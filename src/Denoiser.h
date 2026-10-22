#pragma once

#include <filesystem>
#include <vector>

#include <dpdfnet.h>

#include "Stft.h"

// Streams 48 kHz mono audio through a DPDFNet model, one 10 ms hop at a time.
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

	// Throws std::runtime_error if the CPU lacks AVX2/FMA or the weights can't be loaded.
	Denoiser(dpdfnet_model_id id, const std::filesystem::path& weightsPath);
	~Denoiser();
	Denoiser(const Denoiser&) = delete;
	Denoiser& operator=(const Denoiser&) = delete;

	void reset();
	// Share of the original (noisy) spectrum mixed back in; 0 = full denoising.
	void setNoisyMix(float alpha) { noisyMix = alpha; }
	void process(const float* in, float* out, int count);

private:
	void processFrame();

	const dpdfnet_model_id id;
	dpdfnet_model* model = nullptr;
	std::vector<float> state;
	std::vector<float> spec;
	std::vector<float> specOut;

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
