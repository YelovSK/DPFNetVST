#pragma once

#include <vector>

#define POCKETFFT_NO_MULTITHREADING
#include <pocketfft_hdronly.h>

// Vorbis-windowed real FFT matching the DPDFNet reference (unnormalized forward, 1/n inverse).
// Spectra are interleaved re/im pairs, n/2 + 1 bins.
class Stft
{
public:
	explicit Stft(int n);

	void forward(const float* frame, float* spec);
	void inverse(const float* spec, float* frame);

private:
	int n;
	std::vector<float> window;
	std::vector<float> packed;
	pocketfft::detail::pocketfft_r<float> plan;
};
