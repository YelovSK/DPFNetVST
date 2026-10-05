# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "onnxruntime", "pillow", "soundfile"]
# ///
"""Opens the plugin editor in an off-screen window and saves a screenshot to tests/out/editor.png.

    uv run tests/editor_snapshot.py
"""

import ctypes as C
import ctypes.wintypes as W
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from compare import OUT, PLUGIN, Plugin  # noqa: E402

user32 = C.windll.user32
gdi32 = C.windll.gdi32
user32.SetProcessDpiAwarenessContext(C.c_void_p(-4))  # per-monitor v2, like Equalizer APO's Qt editor

WNDPROC = C.WINFUNCTYPE(C.c_ssize_t, W.HWND, W.UINT, W.WPARAM, W.LPARAM)
user32.DefWindowProcW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user32.DefWindowProcW.restype = C.c_ssize_t
user32.CreateWindowExW.restype = W.HWND
user32.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD, C.c_int, C.c_int, C.c_int, C.c_int,
                                   W.HWND, W.HMENU, W.HINSTANCE, W.LPVOID]


class WNDCLASSW(C.Structure):
    _fields_ = [("style", W.UINT), ("lpfnWndProc", WNDPROC), ("cbClsExtra", C.c_int), ("cbWndExtra", C.c_int),
                ("hInstance", W.HINSTANCE), ("hIcon", W.HICON), ("hCursor", W.HANDLE), ("hbrBackground", W.HBRUSH),
                ("lpszMenuName", W.LPCWSTR), ("lpszClassName", W.LPCWSTR)]


class Rect(C.Structure):
    _fields_ = [("top", C.c_int16), ("left", C.c_int16), ("bottom", C.c_int16), ("right", C.c_int16)]


def pump():
    msg = W.MSG()
    while user32.PeekMessageW(C.byref(msg), None, 0, 0, 1):
        user32.TranslateMessage(C.byref(msg))
        user32.DispatchMessageW(C.byref(msg))


def main():
    proc = WNDPROC(lambda h, m, w, l: user32.DefWindowProcW(h, m, w, l))
    wc = WNDCLASSW(lpfnWndProc=proc, lpszClassName="SnapshotHost", hbrBackground=gdi32.GetStockObject(0))
    user32.RegisterClassW(C.byref(wc))
    host = user32.CreateWindowExW(0x08000000 | 0x80, "SnapshotHost", "host", 0x80000000, -6000, -6000, 1000, 600,
                                  None, None, None, None)  # WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, WS_POPUP

    plugin = Plugin(PLUGIN)
    plugin.start(48000, 512, {0: 1.0, 1: 0.12})
    rect_ptr = C.POINTER(Rect)()
    plugin.dispatch(13, ptr=C.cast(C.byref(rect_ptr), C.c_void_p))
    assert plugin.dispatch(14, ptr=C.c_void_p(host)) == 1
    plugin.dispatch(13, ptr=C.cast(C.byref(rect_ptr), C.c_void_p))
    r = rect_ptr.contents
    w, h = r.right - r.left, r.bottom - r.top
    user32.SetWindowPos(host, None, -6000, -6000, w, h, 0x0010 | 0x0004)  # SWP_NOACTIVATE | SWP_NOZORDER
    user32.ShowWindow(host, 4)  # SW_SHOWNOACTIVATE
    for _ in range(20):
        pump()
        user32.UpdateWindow(host)

    hdc = user32.GetDC(host)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(host, mem, 2)  # PW_RENDERFULLCONTENT
    buf = (C.c_uint8 * (w * h * 4))()

    class BITMAPINFOHEADER(C.Structure):
        _fields_ = [("biSize", W.DWORD), ("biWidth", C.c_long), ("biHeight", C.c_long), ("biPlanes", W.WORD),
                    ("biBitCount", W.WORD), ("biCompression", W.DWORD), ("biSizeImage", W.DWORD),
                    ("biXPelsPerMeter", C.c_long), ("biYPelsPerMeter", C.c_long), ("biClrUsed", W.DWORD),
                    ("biClrImportant", W.DWORD)]

    bih = BITMAPINFOHEADER(C.sizeof(BITMAPINFOHEADER), w, -h, 1, 32, 0)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, C.byref(bih), 0)
    img = np.frombuffer(buf, np.uint8).reshape(h, w, 4)[:, :, [2, 1, 0]]
    OUT.mkdir(parents=True, exist_ok=True)
    Image.fromarray(img).save(OUT / "editor.png")
    print(f"{w}x{h} -> {OUT / 'editor.png'}")

    plugin.dispatch(15)
    plugin.close()
    user32.DestroyWindow(host)


if __name__ == "__main__":
    main()
