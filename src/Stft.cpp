#include "Stft.h"

#include <cmath>
#include <numbers>

Stft::Stft(int n) : n(n), window(n), packed(n), plan(n)
{
	const double half = n / 2.0;
	for (int i = 0; i < n; i++)
	{
		const double s = std::sin(0.5 * std::numbers::pi * (i + 0.5) / half);
		window[i] = (float)std::sin(0.5 * std::numbers::pi * s * s);
	}
}

// pocketfft's packed layout is FFTPACK's: r0, r1, i1, r2, i2, ..., r(n/2) for even n.

void Stft::forward(const float* frame, float* spec)
{
	for (int i = 0; i < n; i++)
		packed[i] = frame[i] * window[i];
	plan.exec(packed.data(), 1.0f, true);

	spec[0] = packed[0];
	spec[1] = 0.0f;
	for (int k = 1; k < n / 2; k++)
	{
		spec[2 * k] = packed[2 * k - 1];
		spec[2 * k + 1] = packed[2 * k];
	}
	spec[n] = packed[n - 1];
	spec[n + 1] = 0.0f;
}

void Stft::inverse(const float* spec, float* frame)
{
	packed[0] = spec[0];
	for (int k = 1; k < n / 2; k++)
	{
		packed[2 * k - 1] = spec[2 * k];
		packed[2 * k] = spec[2 * k + 1];
	}
	packed[n - 1] = spec[n];
	plan.exec(packed.data(), 1.0f / n, false);

	for (int i = 0; i < n; i++)
		frame[i] = packed[i] * window[i];
}
