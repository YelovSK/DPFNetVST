#pragma once

/* Force-included when building the DPDFNet native runtime with MSVC, which lacks the two
   GCC builtins it uses. Keeps the vendored sources unmodified. */

#ifdef _MSC_VER

#include <intrin.h>
#include <string.h>

#define __attribute__(x)

/* MSVC has only the vector form; its immediate takes the rounding mode without the no-exception bit. */
#define _cvtss_sh(x, imm) ((unsigned short)_mm_extract_epi16(_mm_cvtps_ph(_mm_set_ss(x), (imm) & 7), 0))

#define __builtin_cpu_init() ((void)0)
#define __builtin_cpu_supports(feature) dpdf_msvc_cpu_supports(feature)

/* Like GCC's builtin, AVX features only count when the OS saves the YMM registers. */
static int dpdf_msvc_cpu_supports(const char* feature)
{
	int info[4];
	__cpuid(info, 1);
	const int ecx1 = info[2];
	const int osAvx = (ecx1 & (1 << 27)) && (ecx1 & (1 << 28)) && (_xgetbv(0) & 6) == 6;
	if (!osAvx)
		return 0;

	if (strcmp(feature, "fma") == 0)
		return (ecx1 >> 12) & 1;
	if (strcmp(feature, "f16c") == 0)
		return (ecx1 >> 29) & 1;
	if (strcmp(feature, "avx2") == 0)
	{
		__cpuidex(info, 7, 0);
		return (info[1] >> 5) & 1;
	}
	return 0;
}

#endif
