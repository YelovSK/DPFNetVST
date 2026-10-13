#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <dpdfnet2_48khz_hr/generated_model.h>
#include <dpdfnet8_48khz_hr/generated_model.h>

#include "Denoiser.h"
#include "Editor.h"
#include "vst2.h"

class Plugin
{
public:
	enum Param
	{
		paramModel,
		paramAttenLimit,
		paramCount
	};

	struct Model
	{
		const char* name;
		const wchar_t* weightsFile;
		const dpdf_native_api_v1* (*api)(uint32_t abiVersion);
	};

	static constexpr Model models[] = {
		{"DPDFNet-2", L"dpdfnet2_48khz_hr.weights.f32", dpdfnet2_48khz_hr_get_api},
		{"DPDFNet-8", L"dpdfnet8_48khz_hr.weights.f32", dpdfnet8_48khz_hr_get_api},
	};
	static constexpr int modelCount = (int)std::size(models);
	static constexpr float attenLimitMaxDb = 100.0f;

	static int modelIndex(float value);
	static float modelValue(int index);
	static float attenLimitDb(float value);

	explicit Plugin(vst2::HostCallback host);

	vst2::AEffect effect{};

	float getParameter(int index) const;
	void setParameter(int index, float value);
	// Applies a change made in the editor and tells the host, so it can store it.
	void editParameter(int index, float value);

	// Human-readable state for the editor: missing files, load errors, or a summary.
	std::wstring status() const;

private:
	intptr_t dispatch(int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
	void processReplacing(float** inputs, float** outputs, int32_t frames);
	void resume();
	// Loads the selected model unless it was already tried; returns whether it did.
	bool updateModel();
	void fail(const std::string& message);

	vst2::HostCallback host;
	Editor editor{*this};

	std::atomic<float> params[paramCount];
	float sampleRate = 0.0f;
	int connectedInputs = 2;

	std::unique_ptr<Denoiser> denoiser;
	int attemptedModel = -1;
	std::vector<float> mono, denoised;

	mutable std::mutex errorMutex;
	std::string error;
};
