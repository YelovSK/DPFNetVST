#include "Denoiser.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace
{
	std::vector<float> readWeights(const std::filesystem::path& path, size_t count)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file)
			throw std::runtime_error("Could not open " + path.string());
		if ((size_t)file.tellg() != count * sizeof(float))
			throw std::runtime_error(path.string() + " has the wrong size for this model");

		std::vector<float> weights(count);
		file.seekg(0);
		file.read((char*)weights.data(), count * sizeof(float));
		if (!file)
			throw std::runtime_error("Could not read " + path.string());
		return weights;
	}
}

Denoiser::Denoiser(const dpdf_native_api_v1& api, const std::filesystem::path& weightsPath)
	: api(api), state(api.state_size), spec(freqBins * 2), specOut(freqBins * 2), inFrame(winLen),
	  outFrame(winLen), overlap(winLen), outQueue(hopLen), noisyHistory(modelDelayFrames * freqBins * 2)
{
	if (api.sample_rate != sampleRate || api.hop_size != hopLen || api.spectrum_size != spec.size())
		throw std::runtime_error("Model doesn't match the 48 kHz DPDFNet framing");

	const std::vector<float> weights = readWeights(weightsPath, api.weight_count);
	const uint32_t preset =
		api.preset_supported(DPDF_PRESET_INT8_SELECTIVE) ? DPDF_PRESET_INT8_SELECTIVE : DPDF_PRESET_FP32;
	model = api.create(weights.data(), weights.size(), preset);
	if (model == nullptr)
		throw std::runtime_error("Could not create the model from " + weightsPath.string());

	reset();
	// Touch the model's working memory once before real-time use.
	processFrame();
	reset();
}

Denoiser::~Denoiser()
{
	api.destroy(model);
}

void Denoiser::reset()
{
	api.init_state(state.data());
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

	api.process(model, spec.data(), state.data(), specOut.data(), state.data());

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
