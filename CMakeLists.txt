cmake_minimum_required(VERSION 3.25)
project(DPDFNetVST LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# DPDFNet's native C runtime and generated model code, vendored from the commit below.
# The weights are downloaded from the same commit.
set(DPDF_COMMIT 6a5dbd3ea5dbea88c30a2be8e7c689b5eb26b533)
set(DPDF_NATIVE_DIR ${CMAKE_SOURCE_DIR}/third_party/dpdfnet_native)
set(WEIGHTS_URL https://raw.githubusercontent.com/ceva-ip/DPDFNet/${DPDF_COMMIT}/native_inference/artifacts/v1)
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

add_library(dpdf_native STATIC
	${DPDF_NATIVE_DIR}/dpdfnet2_48khz_hr/generated_model.c
	${DPDF_NATIVE_DIR}/dpdfnet8_48khz_hr/generated_model.c
	${DPDF_NATIVE_DIR}/runtime/dpdf_dprnn.c
	${DPDF_NATIVE_DIR}/runtime/full_ops.c
	${DPDF_NATIVE_DIR}/runtime/extended_ops.c
	${DPDF_NATIVE_DIR}/runtime/avx2.c
	${DPDF_NATIVE_DIR}/runtime/fp16.c
	${DPDF_NATIVE_DIR}/runtime/int8.c)
target_include_directories(dpdf_native PUBLIC ${DPDF_NATIVE_DIR} ${DPDF_NATIVE_DIR}/runtime)
# The AVX2 kernels are only called after a runtime CPU check.
target_compile_definitions(dpdf_native PRIVATE DPDF_X86_DISPATCH)
target_compile_options(dpdf_native PRIVATE /FI${DPDF_NATIVE_DIR}/msvc_compat.h /W3 /wd4244 /wd4267 /wd4305 /wd4068 /wd4556)
set_source_files_properties(
	${DPDF_NATIVE_DIR}/runtime/avx2.c ${DPDF_NATIVE_DIR}/runtime/fp16.c ${DPDF_NATIVE_DIR}/runtime/int8.c
	PROPERTIES COMPILE_OPTIONS /arch:AVX2)

add_library(DPDFNet SHARED
	src/Denoiser.cpp
	src/Editor.cpp
	src/Plugin.cpp
	src/Stft.cpp
	src/resources.rc)
target_include_directories(DPDFNet PRIVATE third_party/pocketfft)
target_compile_definitions(DPDFNet PRIVATE NOMINMAX UNICODE _UNICODE)
target_compile_options(DPDFNet PRIVATE /W4 /utf-8)
target_link_libraries(DPDFNet PRIVATE dpdf_native comctl32)
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
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DPDF_NATIVE_DIR}/LICENSE ${PLUGIN_DIR}/DPDFNet/DPDFNet-LICENSE.txt)
