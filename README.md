# DPDFNet VST2

A VST2 plugin for Windows running [DPDFNet](https://github.com/ceva-ip/DPDFNet) speech denoising (48 kHz models) via ONNX Runtime, built for use in Equalizer APO on a microphone.

## Build & install

Needs Visual Studio (C++ workload), CMake and Ninja. ONNX Runtime and the models are downloaded at configure time.

```
build.cmd
install.cmd
```

`install.cmd` copies `build\plugin\DPDFNet.dll` and the `DPDFNet` folder next to it (ONNX Runtime + models) into `C:\Program Files\EqualizerAPO\VSTPlugins`. The folder must stay next to the DLL.

Then add it to the device's config in Equalizer APO, either through the Editor (add a VST plugin, pick `DPDFNet.dll`, open its panel) or by hand:

```
VSTPlugin: Library DPDFNet.dll Model 1 "Attenuation limit" 1
```

## Parameters

- **Model**: `0` = DPDFNet-2 (about 15% of one CPU core), `1` = DPDFNet-8 (better quality, about 45%).
- **Attenuation limit**: maximum noise reduction, stored as dB / 100 (`0.12` = 12 dB). `1` = no limit. Lower values keep more of the original sound, like DeepFilterNet's setting of the same name.

## Behavior

- The device must run at 48 kHz; at other rates the plugin passes audio through untouched.
- Inputs are mixed to mono, denoised once, and written to both outputs. Unconnected inputs (a mono mic) aren't mixed in.
- Latency is 60 ms: 20 ms of STFT framing plus 40 ms of model delay.

## Tests

```
uv run tests/compare.py            # plugin vs. a numpy port of the reference streamer, latency, bypass
uv run tests/editor_snapshot.py    # renders the settings panel to tests/out/editor.png
```

`compare.py` synthesizes test speech with Windows TTS, or takes a 48 kHz WAV as an argument. Outputs land in `tests/out`.
