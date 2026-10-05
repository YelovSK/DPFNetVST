cmake_minimum_required(VERSION 3.25)
project(DPDFNetVST LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT for our own code; onnxruntime.dll still needs the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

set(ORT_VERSION 1.30.0)
FetchContent_Declare(onnxruntime
	URL https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-win-x64-${ORT_VERSION}.zip
	URL_HASH SHA256=c6ba983baf5681af108599675d2a89c2d145512d02de28aed0bff177cd0ba949
	DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(onnxruntime)

set(MODEL_URL https://huggingface.co/Ceva-IP/DPDFNet/resolve/main/onnx)
set(MODEL_DIR ${CMAKE_BINARY_DIR}/models)
set(MODELS
	"dpdfnet2_48khz_hr\;7f0575a5cec0ba4ffd8f8bd657e06d007e4ccdd955d76faab922b9d3291dc14b"
	"dpdfnet8_48khz_hr\;7b3afbb260a08fe9af3d16e3bda992971be1e7e951d1dee7c2d235f5c43f5631")
set(MODEL_FILES)
foreach(entry IN LISTS MODELS)
	list(GET entry 0 name)
	list(GET entry 1 hash)
	file(DOWNLOAD ${MODEL_URL}/${name}.onnx ${MODEL_DIR}/${name}.onnx EXPECTED_HASH SHA256=${hash})
	list(APPEND MODEL_FILES ${MODEL_DIR}/${name}.onnx)
endforeach()

add_library(DPDFNet SHARED
	src/Denoiser.cpp
	src/Editor.cpp
	src/OrtRuntime.cpp
	src/Plugin.cpp
	src/Stft.cpp
	src/resources.rc)
target_include_directories(DPDFNet PRIVATE
	${onnxruntime_SOURCE_DIR}/include
	third_party/pocketfft)
target_compile_definitions(DPDFNet PRIVATE NOMINMAX UNICODE _UNICODE)
target_compile_options(DPDFNet PRIVATE /W4 /utf-8)
target_link_libraries(DPDFNet PRIVATE comctl32)
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
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll ${MODEL_FILES} ${PLUGIN_DIR}/DPDFNet
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${onnxruntime_SOURCE_DIR}/LICENSE ${PLUGIN_DIR}/DPDFNet/onnxruntime-LICENSE.txt)
