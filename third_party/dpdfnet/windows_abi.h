#pragma once

/* Force-included into the runtime's C sources. The assembly kernels use the SysV calling
   convention; clang makes the sources' own plain declarations inherit sysv_abi from these, and
   then saves the registers Windows expects preserved (rsi, rdi, xmm6-15) around each call. */

#include <immintrin.h>
#include <stdint.h>

#define DPDF_SYSV __attribute__((sysv_abi))

DPDF_SYSV void dpdf_qdot64_fixed_avx2(const int8_t *, const int8_t *, int32_t *);
DPDF_SYSV void dpdf_qdot4pair64_avx2(const int8_t *, const int8_t *, const int8_t *, __m256i *);
DPDF_SYSV void dpdf_qaffine64_fused_avx2(const int8_t *, const int8_t *, const int32_t *,
	const float *, const float *, float *, float, int, int);
DPDF_SYSV void dpdf_qaffine256_fused_avx2(const int8_t *, const int8_t *, const int32_t *,
	const float *, const float *, float *, float, int, int);

/* pair_fused_args.h declares the fused-pair kernel next to its argument struct. Its plain
   declaration is renamed out of the way so the struct can be used for the real one. */
#pragma push_macro("dpdf_qaffine4pair64_fused_avx2")
#undef dpdf_qaffine4pair64_fused_avx2
#define dpdf_qaffine4pair64_fused_avx2 dpdf_qaffine4pair64_fused_avx2_plain_declaration
#include "pair_fused_args.h"
#pragma pop_macro("dpdf_qaffine4pair64_fused_avx2")
DPDF_SYSV void dpdf_qaffine4pair64_fused_avx2(const dpdf_pair_fused_args *);
