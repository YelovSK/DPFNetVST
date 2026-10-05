# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "onnxruntime", "soundfile"]
# ///
"""Runs the built plugin through a minimal ctypes VST2 host and checks it against a numpy
port of DPDFNet's reference StreamEnhancer.

    uv run tests/compare.py [input.wav]

Without an input file, speech is synthesized with Windows' text-to-speech and mixed with noise.
"""

import ctypes as C
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
import onnxruntime as ort
import soundfile as sf

ROOT = Path(__file__).resolve().parent.parent
PLUGIN = ROOT / "build" / "plugin" / "DPDFNet.dll"
OUT = ROOT / "tests" / "out"
SR = 48000
WIN = 960
HOP = 480
PIPELINE_DELAY = WIN  # plugin output vs. the reference streamer's output
MODEL_DELAY = 4 * HOP

# --- minimal VST2 host -------------------------------------------------------

HostCallback = C.CFUNCTYPE(C.c_ssize_t, C.c_void_p, C.c_int32, C.c_int32, C.c_ssize_t, C.c_void_p, C.c_float)
Dispatcher = C.CFUNCTYPE(C.c_ssize_t, C.c_void_p, C.c_int32, C.c_int32, C.c_ssize_t, C.c_void_p, C.c_float)
Process = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(C.POINTER(C.c_float)), C.POINTER(C.POINTER(C.c_float)), C.c_int32)
SetParam = C.CFUNCTYPE(None, C.c_void_p, C.c_int32, C.c_float)
GetParam = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_int32)


class AEffect(C.Structure):
    _fields_ = [
        ("magic", C.c_int32), ("dispatcher", Dispatcher), ("process", Process),
        ("setParameter", SetParam), ("getParameter", GetParam),
        ("numPrograms", C.c_int32), ("numParams", C.c_int32), ("numInputs", C.c_int32),
        ("numOutputs", C.c_int32), ("flags", C.c_int32), ("reserved1", C.c_ssize_t),
        ("reserved2", C.c_ssize_t), ("initialDelay", C.c_int32), ("realQualities", C.c_int32),
        ("offQualities", C.c_int32), ("ioRatio", C.c_float), ("object", C.c_void_p),
        ("user", C.c_void_p), ("uniqueID", C.c_int32), ("version", C.c_int32),
        ("processReplacing", Process), ("processDoubleReplacing", C.c_void_p),
        ("future", C.c_char * 56),
    ]


class Plugin:
    def __init__(self, path, connected_inputs=2):
        self.connected = connected_inputs
        self._cb = HostCallback(self._host)
        self.lib = C.CDLL(str(path))
        main = self.lib.VSTPluginMain
        main.restype = C.POINTER(AEffect)
        main.argtypes = [HostCallback]
        self.ptr = main(self._cb)
        self.e = self.ptr.contents
        assert self.e.magic == int.from_bytes(b"VstP", "big")

    def _host(self, effect, opcode, index, value, ptr, opt):
        if opcode == 1:  # audioMasterVersion
            return 2400
        if opcode == 4:  # audioMasterPinConnected: 0 = connected
            return 0 if index < self.connected else 1
        return 0

    def dispatch(self, opcode, index=0, value=0, ptr=None, opt=0.0):
        return self.e.dispatcher(self.ptr, opcode, index, value, ptr, opt)

    def string(self, opcode, index=0):
        buf = C.create_string_buffer(256)
        self.dispatch(opcode, index, 0, C.cast(buf, C.c_void_p))
        return buf.value.decode()

    def start(self, sample_rate, block_size, params):
        self.dispatch(0)  # effOpen
        self.dispatch(10, opt=float(sample_rate))
        self.dispatch(11, value=block_size)
        for i, v in params.items():
            self.e.setParameter(self.ptr, i, v)
        self.dispatch(12, value=1)  # effMainsChanged on

    def process(self, channels, rng, max_block):
        """Feeds the planar (ch, n) input in random-sized blocks; returns (ch, n) output."""
        n_ch, n = channels.shape
        out = np.zeros_like(channels)
        pos = 0
        while pos < n:
            block = min(int(rng.integers(1, max_block + 1)), n - pos)
            ins = [np.ascontiguousarray(channels[c, pos:pos + block]) for c in range(n_ch)]
            outs = [np.zeros(block, np.float32) for _ in range(n_ch)]
            in_arr = (C.POINTER(C.c_float) * n_ch)(*[a.ctypes.data_as(C.POINTER(C.c_float)) for a in ins])
            out_arr = (C.POINTER(C.c_float) * n_ch)(*[a.ctypes.data_as(C.POINTER(C.c_float)) for a in outs])
            self.e.processReplacing(self.ptr, in_arr, out_arr, block)
            for c in range(n_ch):
                out[c, pos:pos + block] = outs[c]
            pos += block
        return out

    def close(self):
        self.dispatch(12, value=0)
        self.dispatch(1)  # effClose


# --- reference -----------------------------------------------------------------


def vorbis_window(n):
    s = np.sin(0.5 * np.pi * (np.arange(n) + 0.5) / (n / 2))
    return np.sin(0.5 * np.pi * s * s).astype(np.float32)


def reference_stream(model_path, audio, noisy_mix=0.0):
    """numpy port of dpdfnet.StreamEnhancer, plus the attenuation-limit blend of dpdfnet.enhance."""
    opts = ort.SessionOptions()
    opts.intra_op_num_threads = 1
    sess = ort.InferenceSession(str(model_path), opts, providers=["CPUExecutionProvider"])
    meta = sess.get_modelmeta().custom_metadata_map
    state = np.zeros(int(meta["state_size"]), np.float32)
    erb = np.array(meta["erb_norm_init"].split(","), np.float32)
    spec_init = np.array(meta["spec_norm_init"].split(","), np.float32)
    state[: len(erb)] = erb
    state[len(erb): len(erb) + len(spec_init)] = spec_init
    names = [i.name for i in sess.get_inputs()]

    w = vorbis_window(WIN)
    ola = np.zeros(WIN, np.float32)
    history = [np.zeros((WIN // 2 + 1, 2), np.float32) for _ in range(4)]
    out = []
    for start in range(0, len(audio) - WIN + 1, HOP):
        spec = np.fft.rfft(audio[start:start + WIN] * w)
        ri = np.stack([spec.real, spec.imag], -1).astype(np.float32)
        enh, state = sess.run(None, {names[0]: ri[None, None], names[1]: state})
        enh = enh[0, 0]
        if noisy_mix > 0:
            enh = noisy_mix * history[0] + (1 - noisy_mix) * enh
        history = history[1:] + [ri]
        frame = np.fft.irfft(enh[:, 0] + 1j * enh[:, 1], n=WIN).astype(np.float32) * w
        ola += frame
        out.append(ola[:HOP].copy())
        ola = np.concatenate([ola[HOP:], np.zeros(HOP, np.float32)])
    return np.concatenate(out)


# --- test signal ---------------------------------------------------------------


def synthesize_speech(path):
    text = (
        "The quick brown fox jumps over the lazy dog. "
        "This is a test of the noise suppression plugin running inside Equalizer A P O. "
        "If you can hear the fan noise disappear while my voice stays clear, it works."
    )
    ps = f"""
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(48000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
$s.SetOutputToWaveFile('{path}', $fmt)
$s.Speak('{text}')
$s.Dispose()
"""
    subprocess.run(["powershell", "-NoProfile", "-Command", ps], check=True)


def make_noisy(clean, rng, snr_db=5.0):
    n = len(clean)
    t = np.arange(n) / SR
    # Fan-like noise: lowpassed white noise plus mains hum and a whine.
    white = rng.standard_normal(n).astype(np.float32)
    fan = np.convolve(white, np.ones(8) / 8, mode="same")
    hum = 0.3 * np.sin(2 * np.pi * 50 * t) + 0.1 * np.sin(2 * np.pi * 150 * t) + 0.05 * np.sin(2 * np.pi * 3100 * t)
    noise = (fan + hum).astype(np.float32)
    gain = np.sqrt(np.mean(clean ** 2) / np.mean(noise ** 2) / 10 ** (snr_db / 10))
    return (clean + gain * noise).astype(np.float32)


def snr_db(reference, test):
    return 10 * np.log10(np.sum(reference ** 2) / max(np.sum((reference - test) ** 2), 1e-20))


def noise_reduction_db(clean, noisy, denoised):
    """How much quieter the speech pauses got, from 50 ms frames where the clean signal is silent."""
    n = 2400
    frames = len(clean) // n
    energy = lambda x: np.mean(x[: frames * n].reshape(frames, n) ** 2, axis=1)
    pauses = energy(clean) < 1e-7
    return 10 * np.log10(energy(denoised)[pauses].mean() / energy(noisy)[pauses].mean())


def best_lag(x, y, max_lag):
    """Lag (in samples) by which y trails x, via FFT cross-correlation."""
    n = len(x) + len(y)
    corr = np.fft.irfft(np.fft.rfft(y, n) * np.conj(np.fft.rfft(x, n)), n)
    return int(np.argmax(corr[: max_lag + 1]))


# --- main ----------------------------------------------------------------------


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(1)

    if len(sys.argv) > 1:
        noisy, sr = sf.read(sys.argv[1], dtype="float32", always_2d=True)
        assert sr == SR, "input must be 48 kHz"
        noisy = noisy.mean(axis=1)
        clean = None
    else:
        speech = OUT / "speech.wav"
        if not speech.exists():
            synthesize_speech(speech)
        clean, sr = sf.read(speech, dtype="float32")
        assert sr == SR
        clean = 0.5 * clean / np.max(np.abs(clean))
        noisy = make_noisy(clean, rng)
        sf.write(OUT / "noisy.wav", noisy, SR)

    stereo = np.stack([noisy, noisy])
    failures = 0

    for model_index, model in enumerate(["dpdfnet2_48khz_hr", "dpdfnet8_48khz_hr"]):
        for limit_db in [None, 12.0]:
            param = 1.0 if limit_db is None else limit_db / 100.0
            plugin = Plugin(PLUGIN, connected_inputs=2)
            plugin.start(SR, 512, {0: float(model_index), 1: param})
            label = f"{plugin.string(7, 0)}, limit {plugin.string(7, 1)}"
            t0 = time.perf_counter()
            out = plugin.process(stereo, rng, max_block=2048)
            elapsed = time.perf_counter() - t0
            plugin.close()

            assert np.array_equal(out[0], out[1]), "channels differ"
            mono = out[0]
            mix = 0.0 if limit_db is None else 10 ** (-limit_db / 20)
            ref = reference_stream(PLUGIN.parent / "DPDFNet" / f"{model}.onnx", noisy, mix)
            aligned = mono[PIPELINE_DELAY: PIPELINE_DELAY + len(ref)]
            ref = ref[: len(aligned)]
            parity = snr_db(ref, aligned)
            ok = parity > 50
            failures += not ok
            line = f"{label:28s} parity vs reference {parity:6.1f} dB {'OK' if ok else 'FAIL'}"
            line += f" | {elapsed / (len(noisy) / SR) * 100:5.1f}% of real time"
            if clean is not None:
                lag = best_lag(clean, mono, 8000)
                delay = PIPELINE_DELAY + MODEL_DELAY
                c = clean[: len(mono) - delay]
                d = mono[delay: delay + len(c)]
                line += f" | lag {lag} (expected {delay}) | noise in pauses {noise_reduction_db(c, noisy[:len(c)], d):5.1f} dB"
                failures += lag != delay
            print(line)
            sf.write(OUT / f"{model}_limit{'off' if limit_db is None else int(limit_db)}.wav", mono, SR)

    # A mono source in a stereo slot: the silent second input must not halve the level.
    plugin = Plugin(PLUGIN, connected_inputs=1)
    plugin.start(SR, 512, {0: 0.0, 1: 0.0})  # 0 dB limit: output is just the delayed input
    out = plugin.process(np.stack([noisy, np.zeros_like(noisy)]), rng, max_block=2048)
    plugin.close()
    delay = PIPELINE_DELAY + MODEL_DELAY
    # The first hop has no preceding frame to overlap with, so it can't reconstruct exactly.
    passthrough = snr_db(noisy[HOP: len(noisy) - delay], out[0][delay + HOP:])
    ok = passthrough > 60
    failures += not ok
    print(f"0 dB limit, 1 connected input: delayed passthrough {passthrough:.1f} dB {'OK' if ok else 'FAIL'}")

    # Wrong sample rate: plain bypass.
    plugin = Plugin(PLUGIN)
    plugin.start(44100, 512, {})
    out = plugin.process(stereo[:, :44100], rng, max_block=512)
    plugin.close()
    ok = np.array_equal(out, stereo[:, :44100])
    failures += not ok
    print(f"44.1 kHz bypass {'OK' if ok else 'FAIL'}")

    print("ALL OK" if failures == 0 else f"{failures} FAILURE(S)")
    sys.exit(failures != 0)


if __name__ == "__main__":
    main()
