#pragma once

#include <filesystem>
#include <string>
#include <vector>

#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

#include "Stft.h"

// Streams 48 kHz mono audio through a DPDFNet ONNX model, one 10 ms hop at a time.
class Denoiser
{
public:
	static constexpr int sampleRate = 48000;
	static constexpr int winLen = 960;
	static constexpr int hopLen = winLen / 2;
	static constexpr int freqBins = winLen / 2 + 1;
	// Model output frame t corresponds to input frame t - 4.
	static constexpr int modelDelayFrames = 4;
	static constexpr int latencySamples = winLen + modelDelayFrames * hopLen;

	// Throws std::exception (incl. Ort::Exception) if the model can't be loaded.
	Denoiser(Ort::Env& env, const std::filesystem::path& modelPath);

	void reset();
	// Share of the original (noisy) spectrum mixed back in; 0 = full denoising.
	void setNoisyMix(float alpha) { noisyMix = alpha; }
	void process(const float* in, float* out, int count);

private:
	void processFrame();

	Ort::Session session{nullptr};
	Ort::IoBinding binding{nullptr};
	Ort::RunOptions runOptions;
	std::string inStateName, outStateName;

	std::vector<float> initState;
	std::vector<float> state[2];
	Ort::Value stateTensor[2]{Ort::Value{nullptr}, Ort::Value{nullptr}};
	int currentState = 0;

	std::vector<float> spec;
	std::vector<float> specOut;
	Ort::Value specTensor{nullptr};
	Ort::Value specOutTensor{nullptr};

	Stft stft{winLen};
	std::vector<float> inFrame;
	std::vector<float> outFrame;
	std::vector<float> overlap;
	std::vector<float> outQueue;
	int inFill = 0;
	int outPos = hopLen;

	std::vector<float> noisyHistory;
	int historyPos = 0;
	float noisyMix = 0.0f;
};
