#pragma once

#include <filesystem>

#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

// Loads onnxruntime.dll from the given path on first use and returns the process-wide environment.
// Throws std::runtime_error if the library can't be loaded.
Ort::Env& ortEnv(const std::filesystem::path& dllPath);
