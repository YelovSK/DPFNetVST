#pragma once

// The subset of the VST 2.4 plugin ABI this plugin uses.

#include <cstdint>

namespace vst2
{
	constexpr int32_t fourCC(char a, char b, char c, char d)
	{
		return (int32_t)((uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)c << 8 | (uint32_t)d);
	}

	constexpr int32_t effectMagic = fourCC('V', 's', 't', 'P');

	struct AEffect;

	using HostCallback = intptr_t(__cdecl*)(AEffect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
	using Dispatcher = intptr_t(__cdecl*)(AEffect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
	using ProcessFunc = void(__cdecl*)(AEffect*, float** inputs, float** outputs, int32_t frames);
	using ProcessDoubleFunc = void(__cdecl*)(AEffect*, double** inputs, double** outputs, int32_t frames);
	using SetParameterFunc = void(__cdecl*)(AEffect*, int32_t index, float value);
	using GetParameterFunc = float(__cdecl*)(AEffect*, int32_t index);

	struct AEffect
	{
		int32_t magic;
		Dispatcher dispatcher;
		ProcessFunc process;
		SetParameterFunc setParameter;
		GetParameterFunc getParameter;
		int32_t numPrograms;
		int32_t numParams;
		int32_t numInputs;
		int32_t numOutputs;
		int32_t flags;
		intptr_t reserved1;
		intptr_t reserved2;
		int32_t initialDelay;
		int32_t realQualities;
		int32_t offQualities;
		float ioRatio;
		void* object;
		void* user;
		int32_t uniqueID;
		int32_t version;
		ProcessFunc processReplacing;
		ProcessDoubleFunc processDoubleReplacing;
		char future[56];
	};

	struct Rect
	{
		int16_t top, left, bottom, right;
	};

	enum EffectFlags : int32_t
	{
		flagHasEditor = 1 << 0,
		flagCanReplacing = 1 << 4,
		flagNoSoundInStop = 1 << 9,
	};

	enum EffectOpcode : int32_t
	{
		effOpen = 0,
		effClose = 1,
		effSetProgram = 2,
		effGetProgram = 3,
		effSetProgramName = 4,
		effGetProgramName = 5,
		effGetParamLabel = 6,
		effGetParamDisplay = 7,
		effGetParamName = 8,
		effSetSampleRate = 10,
		effSetBlockSize = 11,
		effMainsChanged = 12,
		effEditGetRect = 13,
		effEditOpen = 14,
		effEditClose = 15,
		effEditIdle = 19,
		effCanBeAutomated = 26,
		effGetProgramNameIndexed = 29,
		effGetPlugCategory = 35,
		effGetEffectName = 45,
		effGetVendorString = 47,
		effGetProductString = 48,
		effGetVendorVersion = 49,
		effCanDo = 51,
		effGetTailSize = 52,
		effGetVstVersion = 58,
	};

	enum HostOpcode : int32_t
	{
		audioMasterAutomate = 0,
		audioMasterVersion = 1,
		audioMasterPinConnected = 4,
		audioMasterBeginEdit = 43,
		audioMasterEndEdit = 44,
	};

	constexpr int32_t plugCategoryRestoration = 8;
}
