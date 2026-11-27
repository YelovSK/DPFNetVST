cmake_minimum_required(VERSION 3.25)
project(DPDFNetVST LANGUAGES C CXX ASM RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
	message(FATAL_ERROR "Build with clang-cl (build.cmd does): DPDFNet's assembly kernels need GNU assembler syntax and sysv_abi.")
endif()

# DPDFNet's optimized inference runtime, vendored from the commit below.
# The weights are downloaded from the same commit.
set(DPDF_COMMIT 98df76589cdf7787553b9e05a112d5820e6c8db6)
set(DPDF_DIR ${CMAKE_SOURCE_DIR}/third_party/dpdfnet)
set(WEIGHTS_URL https://raw.githubusercontent.com/ceva-ip/DPDFNet/${DPDF_COMMIT}/optimized_inference/models)
set(WEIGHTS_DIR ${CMAKE_BINARY_DIR}/weights)
set(MODELS
	"dpdfnet2_48khz_hr\;cb7248b8fccbff7b32f254ec2ed0061e604514b2ccd98d997cd081a876424e7b"
	"dpdfnet8_48khz_hr\;5a5bb67a8619090c54dc2793536fe813fe38123b236d445f3433aafe3075529d")
set(WEIGHT_FILES)
foreach(entry IN LISTS MODELS)
	list(GET entry 0 name)
	list(GET entry 1 hash)
	file(DOWNLOAD ${WEIGHTS_URL}/${name}/weights.f32 ${WEIGHTS_DIR}/${name}.weights.f32 EXPECTED_HASH SHA256=${hash})
	list(APPEND WEIGHT_FILES ${WEIGHTS_DIR}/${name}.weights.f32)
endforeach()

# The kernels are written for Linux; COFF has no ELF symbol types, sizes, visibility or stack notes,
# and these leaf functions need no unwind info. Copies without those directives go in the build tree.
function(dpdf_coff_asm out_var)
	set(outputs)
	foreach(source IN LISTS ARGN)
		set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DPDF_DIR}/src/${source})
		file(READ ${DPDF_DIR}/src/${source} text)
		string(REGEX REPLACE "\n[ \t]*\\.(type|size|hidden|cfi_[a-z_]+|section[ \t]+\\.note\\.GNU-stack)[^\n]*" "" text "\n${text}")
		set(output ${CMAKE_BINARY_DIR}/dpdf_asm/${source})
		file(WRITE ${output}.tmp "${text}")
		file(COPY_FILE ${output}.tmp ${output} ONLY_IF_DIFFERENT)
		list(APPEND outputs ${output})
	endforeach()
	set(${out_var} ${outputs} PARENT_SCOPE)
endfunction()

# Each model compiles the shared sources once, with its own symbol prefix.
foreach(size IN ITEMS 2 8)
	set(model dpdfnet${size}_48khz_hr)
	set(asm common/qdot64_fixed_avx2.S common/qdot4pair_fixed_avx2.S models/${model}/qaffine64_fused_avx2.S)
	set(kernels DPDF_QASM64 DPDF_QFUSED64 DPDF_QPAIR_FIXED)
	# Model 8 borrows model 2's fused-pair kernel for its batched affines.
	list(APPEND asm models/dpdfnet2_48khz_hr/qaffine4pair64_fused_avx2.S)
	if(size EQUAL 2)
		list(APPEND asm models/${model}/qaffine256_fused_avx2.S)
		list(APPEND kernels DPDF_QFUSED256 DPDF_QPAIR_FUSED64)
	else()
		list(APPEND kernels DPDF_QBATCH_FUSED64_AVX2)
	endif()
	dpdf_coff_asm(asm_sources ${asm})
	add_library(dpdf_backend${size} OBJECT
		${DPDF_DIR}/src/model_backend.c
		${DPDF_DIR}/src/models/${model}/generated_model.c
		${DPDF_DIR}/src/common/full_ops.c
		${DPDF_DIR}/src/common/dpdf_dprnn.c
		${DPDF_DIR}/src/common/avx2.c
		${DPDF_DIR}/src/common/int8.c
		${DPDF_DIR}/src/common/extended_ops.c
		${DPDF_DIR}/src/common/fp16.c
		${asm_sources})
	target_include_directories(dpdf_backend${size} PRIVATE ${DPDF_DIR}/include ${DPDF_DIR}/src ${DPDF_DIR}/src/private)
	target_compile_definitions(dpdf_backend${size} PRIVATE DPDF_MODEL_SIZE=${size} DPDF_X86_DISPATCH ${kernels})
	target_compile_options(dpdf_backend${size} PRIVATE
		/FI${DPDF_DIR}/src/private/namespace${size}.h
		$<$<COMPILE_LANGUAGE:C>:/FI${DPDF_DIR}/windows_abi.h /clang:-ffp-contract=off /clang:-fno-math-errno>)
endforeach()
# Upstream only measured a gain from branch alignment on model 8.
target_compile_options(dpdf_backend8 PRIVATE /clang:-mbranches-within-32B-boundaries)
# The AVX2 code is only called after a runtime CPU check.
set_source_files_properties(${DPDF_DIR}/src/common/avx2.c ${DPDF_DIR}/src/common/int8.c
	PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
set_source_files_properties(${DPDF_DIR}/src/common/fp16.c PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;-mf16c")

add_library(dpdfnet_runtime STATIC ${DPDF_DIR}/src/runtime.c $<TARGET_OBJECTS:dpdf_backend2> $<TARGET_OBJECTS:dpdf_backend8>)
target_include_directories(dpdfnet_runtime PUBLIC ${DPDF_DIR}/include)
# __builtin_cpu_supports lives in clang's runtime library, which clang-cl doesn't link by default.
execute_process(COMMAND ${CMAKE_C_COMPILER} /clang:--rtlib=compiler-rt /clang:-print-libgcc-file-name
	OUTPUT_VARIABLE CLANG_BUILTINS OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
target_link_libraries(dpdfnet_runtime PRIVATE ${CLANG_BUILTINS})

add_library(DPDFNet SHARED
	src/Denoiser.cpp
	src/Editor.cpp
	src/Plugin.cpp
	src/Stft.cpp
	src/resources.rc)
target_include_directories(DPDFNet PRIVATE third_party/pocketfft)
target_compile_definitions(DPDFNet PRIVATE NOMINMAX UNICODE _UNICODE)
target_compile_options(DPDFNet PRIVATE /W4 /utf-8 -Wno-missing-field-initializers)
target_link_libraries(DPDFNet PRIVATE dpdfnet_runtime comctl32)
# The manifest is embedded via resources.rc for ISOLATION_AWARE_ENABLED.
target_link_options(DPDFNet PRIVATE /MANIFEST:NO)

# Output is laid out the way it gets installed: DPDFNet.dll plus a DPDFNet folder beside it.
set(PLUGIN_DIR ${CMAKE_BINARY_DIR}/plugin)
set_target_properties(DPDFNet PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${PLUGIN_DIR}
	RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PLUGIN_DIR}
	RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PLUGIN_DIR})
add_custom_command(TARGET DPDFNet POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory ${PLUGIN_DIR}/DPDFNet
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WEIGHT_FILES} ${PLUGIN_DIR}/DPDFNet
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DPDF_DIR}/LICENSE ${PLUGIN_DIR}/DPDFNet/DPDFNet-LICENSE.txt
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/third_party/pocketfft/LICENSE.md ${PLUGIN_DIR}/DPDFNet/pocketfft-LICENSE.txt)
