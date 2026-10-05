#include "OrtRuntime.h"

#include <mutex>
#include <stdexcept>

#include <windows.h>

Ort::Env& ortEnv(const std::filesystem::path& dllPath)
{
	static std::mutex mutex;
	static Ort::Env* env = nullptr;

	std::lock_guard lock(mutex);
	if (env != nullptr)
		return *env;

	// Windows ships its own (older) onnxruntime.dll in System32, which would win a normal
	// import-table lookup, so ours is loaded by full path.
	HMODULE lib = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (lib == nullptr)
		throw std::runtime_error("Could not load " + dllPath.string());

	using GetApiBase = const OrtApiBase*(ORT_API_CALL*)();
	auto getApiBase = (GetApiBase)GetProcAddress(lib, "OrtGetApiBase");
	if (getApiBase == nullptr)
		throw std::runtime_error(dllPath.string() + " is not an ONNX Runtime library");

	const OrtApi* api = getApiBase()->GetApi(ORT_API_VERSION);
	if (api == nullptr)
		throw std::runtime_error(dllPath.string() + " is too old (needs ONNX Runtime API " + std::to_string(ORT_API_VERSION) + ")");
	Ort::InitApi(api);

	// Never released: tearing ONNX Runtime down while DLLs are unloading isn't safe.
	env = new Ort::Env(ORT_LOGGING_LEVEL_ERROR, "DPDFNet");
	return *env;
}
