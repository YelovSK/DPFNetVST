#include "Denoiser.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace
{
	std::string metadata(const Ort::ModelMetadata& meta, const char* key)
	{
		Ort::AllocatorWithDefaultOptions allocator;
		auto value = meta.LookupCustomMetadataMapAllocated(key, allocator);
		if (!value)
			throw std::runtime_error(std::string("Model is missing metadata '") + key + "'");
		return value.get();
	}

	std::vector<float> parseFloats(const std::string& csv)
	{
		std::vector<float> values;
		std::istringstream stream(csv);
		std::string item;
		while (std::getline(stream, item, ','))
			values.push_back(std::stof(item));
		return values;
	}
}

Denoiser::Denoiser(Ort::Env& env, const std::filesystem::path& modelPath)
	: spec(freqBins * 2), specOut(freqBins * 2), inFrame(winLen), outFrame(winLen),
	  overlap(winLen), outQueue(hopLen), noisyHistory(modelDelayFrames * freqBins * 2)
{
	Ort::SessionOptions options;
	options.SetIntraOpNumThreads(1);
	options.SetInterOpNumThreads(1);
	options.SetExecutionMode(ORT_SEQUENTIAL);
	options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
	options.AddConfigEntry("session.intra_op.allow_spinning", "0");
	options.AddConfigEntry("session.set_denormal_as_zero", "1");
	session = Ort::Session(env, modelPath.c_str(), options);

	Ort::ModelMetadata meta = session.GetModelMetadata();
	if (std::stoi(metadata(meta, "sample_rate")) != sampleRate || std::stoi(metadata(meta, "n_fft")) != winLen ||
		std::stoi(metadata(meta, "hop_length")) != hopLen)
		throw std::runtime_error("Model is not a 48 kHz DPDFNet model");

	const size_t stateSize = std::stoul(metadata(meta, "state_size"));
	const std::vector<float> erbNormInit = parseFloats(metadata(meta, "erb_norm_init"));
	const std::vector<float> specNormInit = parseFloats(metadata(meta, "spec_norm_init"));
	if (erbNormInit.size() + specNormInit.size() > stateSize)
		throw std::runtime_error("Model state metadata is inconsistent");

	// The state vector starts with the running ERB and spectrum normalization, the rest (RNN state) starts at zero.
	initState.assign(stateSize, 0.0f);
	std::copy(erbNormInit.begin(), erbNormInit.end(), initState.begin());
	std::copy(specNormInit.begin(), specNormInit.end(), initState.begin() + erbNormInit.size());

	Ort::AllocatorWithDefaultOptions allocator;
	const std::string inSpecName = session.GetInputNameAllocated(0, allocator).get();
	inStateName = session.GetInputNameAllocated(1, allocator).get();
	const std::string outSpecName = session.GetOutputNameAllocated(0, allocator).get();
	outStateName = session.GetOutputNameAllocated(1, allocator).get();

	const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
	const int64_t specShape[] = {1, 1, freqBins, 2};
	const int64_t stateShape[] = {(int64_t)stateSize};
	specTensor = Ort::Value::CreateTensor<float>(memory, spec.data(), spec.size(), specShape, 4);
	specOutTensor = Ort::Value::CreateTensor<float>(memory, specOut.data(), specOut.size(), specShape, 4);
	for (int i = 0; i < 2; i++)
	{
		state[i].resize(stateSize);
		stateTensor[i] = Ort::Value::CreateTensor<float>(memory, state[i].data(), stateSize, stateShape, 1);
	}

	binding = Ort::IoBinding(session);
	binding.BindInput(inSpecName.c_str(), specTensor);
	binding.BindOutput(outSpecName.c_str(), specOutTensor);

	reset();
	// The first run does lazy allocations and is much slower; get it out of the way before real-time use.
	processFrame();
	reset();
}

void Denoiser::reset()
{
	state[0] = initState;
	currentState = 0;
	std::fill(inFrame.begin(), inFrame.end(), 0.0f);
	std::fill(overlap.begin(), overlap.end(), 0.0f);
	std::fill(noisyHistory.begin(), noisyHistory.end(), 0.0f);
	inFill = 0;
	outPos = hopLen;
	historyPos = 0;
}

void Denoiser::process(const float* in, float* out, int count)
{
	// Output lags input by exactly one window: each sample is emitted one window after it arrived,
	// at which point the frame that completes it has been processed.
	while (count > 0)
	{
		const int n = std::min(count, winLen - inFill);
		for (int i = 0; i < n; i++)
			out[i] = outPos < hopLen ? outQueue[outPos++] : 0.0f;

		std::memcpy(inFrame.data() + inFill, in, n * sizeof(float));
		inFill += n;
		if (inFill == winLen)
			processFrame();

		in += n;
		out += n;
		count -= n;
	}
}

void Denoiser::processFrame()
{
	stft.forward(inFrame.data(), spec.data());

	binding.BindInput(inStateName.c_str(), stateTensor[currentState]);
	binding.BindOutput(outStateName.c_str(), stateTensor[1 - currentState]);
	session.Run(runOptions, binding);
	currentState = 1 - currentState;

	// The enhanced frame lags the input by modelDelayFrames, so the noisy frame mixed back in is
	// the one from that many frames ago.
	float* delayedNoisy = noisyHistory.data() + historyPos * freqBins * 2;
	if (noisyMix > 0.0f)
	{
		for (int i = 0; i < freqBins * 2; i++)
			specOut[i] = noisyMix * delayedNoisy[i] + (1.0f - noisyMix) * specOut[i];
	}
	std::memcpy(delayedNoisy, spec.data(), spec.size() * sizeof(float));
	historyPos = (historyPos + 1) % modelDelayFrames;

	// The Vorbis window satisfies w[n]^2 + w[n + hop]^2 == 1, so plain overlap-add reconstructs.
	stft.inverse(specOut.data(), outFrame.data());
	for (int i = 0; i < winLen; i++)
		overlap[i] += outFrame[i];
	std::memcpy(outQueue.data(), overlap.data(), hopLen * sizeof(float));
	std::memmove(overlap.data(), overlap.data() + hopLen, (winLen - hopLen) * sizeof(float));
	std::fill(overlap.begin() + (winLen - hopLen), overlap.end(), 0.0f);
	outPos = 0;

	std::memmove(inFrame.data(), inFrame.data() + hopLen, (winLen - hopLen) * sizeof(float));
	inFill = winLen - hopLen;
}
