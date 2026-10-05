#pragma once

#include <windows.h>

#include "vst2.h"

class Plugin;

// Native Win32 settings panel, embedded into the host's window.
class Editor
{
public:
	explicit Editor(Plugin& plugin) : plugin(plugin) {}
	~Editor() { close(); }
	Editor(const Editor&) = delete;
	Editor& operator=(const Editor&) = delete;

	vst2::Rect* rect();
	bool open(HWND parent);
	void close();

private:
	static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam);
	int scale(int value) const { return MulDiv(value, dpi, 96); }
	HWND createControl(const wchar_t* className, const wchar_t* text, DWORD style, int x, int y, int w, int h);
	void showLimit(int db);
	void showStatus();

	Plugin& plugin;
	vst2::Rect bounds{};
	int dpi = 96;
	HWND window = nullptr;
	HWND modelBox = nullptr;
	HWND limitSlider = nullptr;
	HWND limitText = nullptr;
	HWND statusText = nullptr;
	HFONT font = nullptr;
};
