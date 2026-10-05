// Use the comctl32 v6 manifest embedded in the DLL, so controls get the modern visual style
// even inside hosts that don't enable it themselves.
#define ISOLATION_AWARE_ENABLED 1

#include "Editor.h"

#include <cmath>
#include <cstdio>

#include <commctrl.h>

#include "Plugin.h"

namespace
{
	constexpr wchar_t className[] = L"DPDFNetEditor";
	constexpr int width = 400;
	constexpr int height = 132;
	constexpr int labelWidth = 120;
	constexpr int controlX = 140;

	HINSTANCE moduleHandle()
	{
		HMODULE module = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCWSTR)&moduleHandle, &module);
		return module;
	}

	int limitSliderPosition(float value)
	{
		return (int)std::lround(Plugin::attenLimitDb(value));
	}
}

vst2::Rect* Editor::rect()
{
	if (window == nullptr)
		dpi = (int)GetDpiForSystem();
	bounds = {0, 0, (int16_t)scale(height), (int16_t)scale(width)};
	return &bounds;
}

bool Editor::open(HWND parent)
{
	close();

	INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&controls);

	// Registered per open and unregistered on close: a class left behind would point at
	// a stale window procedure once the host unloads and reloads the DLL.
	WNDCLASSEXW wc{sizeof(wc)};
	wc.lpfnWndProc = windowProc;
	wc.hInstance = moduleHandle();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
	wc.lpszClassName = className;
	RegisterClassExW(&wc);

	dpi = (int)GetDpiForWindow(parent);
	window = CreateWindowExW(0, className, L"DPDFNet", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, scale(width),
		scale(height), parent, nullptr, moduleHandle(), this);
	if (window == nullptr)
		return false;

	NONCLIENTMETRICSW metrics{sizeof(metrics)};
	SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi);
	font = CreateFontIndirectW(&metrics.lfMessageFont);

	createControl(WC_STATICW, L"Model", SS_LEFT, 12, 16, labelWidth, 20);
	modelBox = createControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, controlX, 12, 248, 200);
	SendMessageW(modelBox, CB_ADDSTRING, 0, (LPARAM)L"DPDFNet-2 (lighter)");
	SendMessageW(modelBox, CB_ADDSTRING, 0, (LPARAM)L"DPDFNet-8 (best quality)");
	SendMessageW(modelBox, CB_SETCURSEL, Plugin::modelIndex(plugin.getParameter(Plugin::paramModel)), 0);

	createControl(WC_STATICW, L"Attenuation limit", SS_LEFT, 12, 54, labelWidth, 20);
	limitSlider = createControl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, controlX - 6, 50, 196, 28);
	SendMessageW(limitSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, (int)Plugin::attenLimitMaxDb));
	SendMessageW(limitSlider, TBM_SETPAGESIZE, 0, 5);
	SendMessageW(limitSlider, TBM_SETPOS, TRUE, limitSliderPosition(plugin.getParameter(Plugin::paramAttenLimit)));
	limitText = createControl(WC_STATICW, L"", SS_LEFT, controlX + 196, 54, 56, 20);
	showLimit(limitSliderPosition(plugin.getParameter(Plugin::paramAttenLimit)));

	statusText = createControl(WC_STATICW, L"", SS_LEFT, 12, 88, width - 24, 36);
	showStatus();
	return true;
}

void Editor::close()
{
	if (window != nullptr)
	{
		DestroyWindow(window);
		window = nullptr;
	}
	if (font != nullptr)
	{
		DeleteObject(font);
		font = nullptr;
	}
	UnregisterClassW(className, moduleHandle());
}

HWND Editor::createControl(const wchar_t* controlClass, const wchar_t* text, DWORD style, int x, int y, int w, int h)
{
	HWND control = CreateWindowExW(0, controlClass, text, WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(w),
		scale(h), window, nullptr, moduleHandle(), nullptr);
	SendMessageW(control, WM_SETFONT, (WPARAM)font, FALSE);
	return control;
}

void Editor::showLimit(int db)
{
	wchar_t text[16];
	if (db >= (int)Plugin::attenLimitMaxDb)
		wcscpy_s(text, L"Off");
	else
		swprintf_s(text, L"%d dB", db);
	SetWindowTextW(limitText, text);
}

void Editor::showStatus()
{
	SetWindowTextW(statusText, plugin.status().c_str());
}

LRESULT CALLBACK Editor::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_NCCREATE)
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams);

	auto editor = (Editor*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (editor == nullptr || editor->window != hwnd)
		return DefWindowProcW(hwnd, message, wParam, lParam);
	return editor->handle(message, wParam, lParam);
}

LRESULT Editor::handle(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_COMMAND:
		if ((HWND)lParam == modelBox && HIWORD(wParam) == CBN_SELCHANGE)
		{
			const int index = (int)SendMessageW(modelBox, CB_GETCURSEL, 0, 0);
			plugin.editParameter(Plugin::paramModel, Plugin::modelValue(index));
			showStatus();
		}
		return 0;

	case WM_HSCROLL:
		if ((HWND)lParam == limitSlider)
		{
			const int db = (int)SendMessageW(limitSlider, TBM_GETPOS, 0, 0);
			showLimit(db);
			// Equalizer APO rebuilds its whole filter chain on every stored change,
			// so only commit once the drag or key press is over.
			if (LOWORD(wParam) == TB_ENDTRACK)
				plugin.editParameter(Plugin::paramAttenLimit, db / Plugin::attenLimitMaxDb);
		}
		return 0;

	case WM_CTLCOLORSTATIC:
	{
		HDC dc = (HDC)wParam;
		SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
		SetTextColor(dc, GetSysColor((HWND)lParam == statusText ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
		return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
	}
	}

	return DefWindowProcW(window, message, wParam, lParam);
}
