#include "Plugin.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <windows.h>


namespace
{
	constexpr int32_t pluginVersion = 1000;
	constexpr int maxChunk = 1024;

	// Model weights live in a folder next to the plugin DLL.
	std::filesystem::path dataDir()
	{
		HMODULE module = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCWSTR)&dataDir, &module);
		std::wstring path(MAX_PATH, L'\0');
		DWORD length;
		while ((length = GetModuleFileNameW(module, path.data(), (DWORD)path.size())) == path.size())
			path.resize(path.size() * 2);
		path.resize(length);
		return std::filesystem::path(path).parent_path() / L"DPDFNet";
	}

	std::filesystem::path weightsPath(int index) { return dataDir() / Plugin::models[index].weightsFile; }

	void copyString(void* dest, const char* src, size_t size)
	{
		strncpy_s((char*)dest, size, src, _TRUNCATE);
	}

	Plugin* self(vst2::AEffect* effect) { return (Plugin*)effect->object; }
}

int Plugin::modelIndex(float value)
{
	return std::clamp((int)std::lround(value * (modelCount - 1)), 0, modelCount - 1);
}

float Plugin::modelValue(int index)
{
	return (float)index / (modelCount - 1);
}

float Plugin::attenLimitDb(float value)
{
	return value * attenLimitMaxDb;
}

Plugin::Plugin(vst2::HostCallback host) : host(host)
{
	params[paramModel] = modelValue(modelCount - 1);
	params[paramAttenLimit] = 1.0f;

	effect.magic = vst2::effectMagic;
	effect.object = this;
	effect.dispatcher = [](vst2::AEffect* e, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt) {
		return self(e)->dispatch(opcode, index, value, ptr, opt);
	};
	effect.setParameter = [](vst2::AEffect* e, int32_t index, float value) { self(e)->setParameter(index, value); };
	effect.getParameter = [](vst2::AEffect* e, int32_t index) { return self(e)->getParameter(index); };
	effect.processReplacing = [](vst2::AEffect* e, float** in, float** out, int32_t frames) {
		self(e)->processReplacing(in, out, frames);
	};
	// Legacy accumulating process(); only used by hosts that ignore flagCanReplacing.
	effect.process = [](vst2::AEffect* e, float** in, float** out, int32_t frames) {
		self(e)->processReplacing(in, out, frames);
	};
	effect.numPrograms = 1;
	effect.numParams = paramCount;
	effect.numInputs = 2;
	effect.numOutputs = 2;
	effect.flags = vst2::flagCanReplacing | vst2::flagHasEditor | vst2::flagNoSoundInStop;
	effect.initialDelay = Denoiser::latencySamples;
	effect.ioRatio = 1.0f;
	effect.uniqueID = vst2::fourCC('D', 'P', 'D', 'F');
	effect.version = pluginVersion;
}

float Plugin::getParameter(int index) const
{
	return index >= 0 && index < paramCount ? params[index].load() : 0.0f;
}

void Plugin::setParameter(int index, float value)
{
	if (index >= 0 && index < paramCount)
		params[index] = std::clamp(value, 0.0f, 1.0f);
}

void Plugin::editParameter(int index, float value)
{
	setParameter(index, value);
	host(&effect, vst2::audioMasterBeginEdit, index, 0, nullptr, 0.0f);
	host(&effect, vst2::audioMasterAutomate, index, 0, nullptr, getParameter(index));
	host(&effect, vst2::audioMasterEndEdit, index, 0, nullptr, 0.0f);
}

std::wstring Plugin::status() const
{
	{
		std::lock_guard lock(errorMutex);
		if (!error.empty())
			return std::wstring(error.begin(), error.end());
	}

	const std::filesystem::path weights = weightsPath(modelIndex(getParameter(paramModel)));
	if (!std::filesystem::exists(weights))
		return L"Missing " + weights.wstring();

	if (sampleRate != 0.0f && sampleRate != Denoiser::sampleRate)
		return L"Bypassed: the device must run at 48 kHz";

	wchar_t text[128];
	swprintf_s(text, L"Latency %.0f ms. Mixes input to mono; needs 48 kHz.",
		1000.0 * Denoiser::latencySamples / Denoiser::sampleRate);
	return text;
}

void Plugin::fail(const std::string& message)
{
	OutputDebugStringA(("DPDFNet: " + message + "\n").c_str());
	std::lock_guard lock(errorMutex);
	error = message;
}

bool Plugin::updateModel()
{
	const int index = modelIndex(getParameter(paramModel));
	if (sampleRate != Denoiser::sampleRate || index == attemptedModel)
		return false;

	attemptedModel = index;
	denoiser.reset();
	{
		std::lock_guard lock(errorMutex);
		error.clear();
	}

	try
	{
		const dpdf_native_api_v1* api = models[index].api(DPDF_NATIVE_ABI_VERSION);
		if (api == nullptr)
			throw std::runtime_error("Native model API version mismatch");
		denoiser = std::make_unique<Denoiser>(*api, weightsPath(index));
	}
	catch (const std::exception& e)
	{
		fail(e.what());
	}
	return true;
}

void Plugin::resume()
{
	if (!updateModel() && denoiser)
		denoiser->reset();

	// Hosts feed silence into unconnected inputs; averaging them in would lower the level.
	// Note the VST quirk: 0 means connected.
	connectedInputs = 0;
	for (int i = 0; i < effect.numInputs; i++)
	{
		if (host(&effect, vst2::audioMasterPinConnected, i, 0, nullptr, 0.0f) == 0)
			connectedInputs++;
	}
	connectedInputs = std::max(connectedInputs, 1);
}

void Plugin::processReplacing(float** inputs, float** outputs, int32_t frames)
{
	updateModel();

	if (!denoiser || sampleRate != Denoiser::sampleRate)
	{
		for (int c = 0; c < effect.numOutputs; c++)
		{
			if (outputs[c] != inputs[c])
				std::memcpy(outputs[c], inputs[c], frames * sizeof(float));
		}
		return;
	}

	const float limitDb = attenLimitDb(getParameter(paramAttenLimit));
	denoiser->setNoisyMix(limitDb >= attenLimitMaxDb ? 0.0f : std::pow(10.0f, -limitDb / 20.0f));

	try
	{
		for (int offset = 0; offset < frames; offset += maxChunk)
		{
			const int n = std::min(frames - offset, maxChunk);
			for (int i = 0; i < n; i++)
			{
				float sum = 0.0f;
				for (int c = 0; c < connectedInputs; c++)
					sum += inputs[c][offset + i];
				mono[i] = sum / connectedInputs;
			}

			denoiser->process(mono.data(), denoised.data(), n);

			for (int c = 0; c < effect.numOutputs; c++)
				std::memcpy(outputs[c] + offset, denoised.data(), n * sizeof(float));
		}
	}
	catch (const std::exception& e)
	{
		denoiser.reset();
		fail(e.what());
	}
}

intptr_t Plugin::dispatch(int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
{
	using namespace vst2;

	switch (opcode)
	{
	case effOpen:
		mono.resize(maxChunk);
		denoised.resize(maxChunk);
		return 0;

	case effClose:
		delete this;
		return 0;

	case effGetProgram:
		return 0;

	case effGetProgramName:
	case effGetProgramNameIndexed:
		copyString(ptr, "Default", 24);
		return 1;

	case effGetParamName:
		if (index == paramModel)
			copyString(ptr, "Model", 32);
		else if (index == paramAttenLimit)
			copyString(ptr, "Attenuation limit", 32);
		return 0;

	case effGetParamLabel:
		copyString(ptr, index == paramAttenLimit && attenLimitDb(getParameter(index)) < attenLimitMaxDb ? "dB" : "", 8);
		return 0;

	case effGetParamDisplay:
		if (index == paramModel)
			copyString(ptr, models[modelIndex(getParameter(index))].name, 24);
		else if (index == paramAttenLimit)
		{
			const float db = attenLimitDb(getParameter(index));
			char text[24];
			if (db >= attenLimitMaxDb)
				strcpy_s(text, "Off");
			else
				sprintf_s(text, "%.1f", db);
			copyString(ptr, text, 24);
		}
		return 0;

	case effSetSampleRate:
		sampleRate = opt;
		return 0;

	case effMainsChanged:
		if (value != 0)
			resume();
		return 0;

	case effEditGetRect:
		*(Rect**)ptr = editor.rect();
		return 1;

	case effEditOpen:
		return editor.open((HWND)ptr) ? 1 : 0;

	case effEditClose:
		editor.close();
		return 0;

	case effCanBeAutomated:
		return 1;

	case effGetPlugCategory:
		return plugCategoryRestoration;

	case effGetEffectName:
	case effGetProductString:
		copyString(ptr, "DPDFNet", 32);
		return 1;

	case effGetVendorString:
		copyString(ptr, "YelovSK", 64);
		return 1;

	case effGetVendorVersion:
		return pluginVersion;

	case effGetVstVersion:
		return 2400;

	default:
		return 0;
	}
}

extern "C" __declspec(dllexport) vst2::AEffect* VSTPluginMain(vst2::HostCallback host)
{
	if (host(nullptr, vst2::audioMasterVersion, 0, 0, nullptr, 0.0f) == 0)
		return nullptr;
	return &(new Plugin(host))->effect;
}
