"""Render a minimum-pitch stereo fixture in an isolated Windows REAPER profile."""

import argparse
from array import array
from base64 import b64decode, b64encode
from math import cos, exp, pi, sin
from pathlib import Path
import struct
import subprocess
import tempfile
import textwrap
import time
import wave
from xml.sax.saxutils import quoteattr


RATE = 48000
PITCH_STEP = 2 ** (-25 / 12)
STRIKE = RATE * 3 // 4
RENDER_FRAMES = round(RATE * 43.4)


def write_wav(path, channels, samples):
    with wave.open(str(path), "wb") as audio:
        audio.setparams((channels, 2, RATE, 0, "NONE", ""))
        audio.writeframes(samples.tobytes())


def make_fixture(folder, repo):
    stereo = array("h")
    for frame in range(10 * RATE):
        t = frame / RATE
        envelope = 8192 * exp(-0.18 * t) * min(1, (10 - t) / 0.06)
        stereo.append(round(envelope * sin(2 * pi * 880 * t)))
        stereo.append(round(0.65 * envelope * sin(2 * pi * 1320 * t)) if t >= 0.25 else 0)
    sample = folder / "stereo-decay.wav"
    write_wav(sample, 2, stereo)
    dry = array("h", [0]) * RATE
    for frame in range(1000):
        t = frame / RATE
        dry[STRIKE + frame] = round(3932 * exp(-t / 0.0008) * cos(2 * pi * 4300 * t))
    write_wav(folder / "input.wav", 1, dry)

    lines = (repo / "PadTrigger - One-track stereo.RTrackTemplate").read_text(encoding="utf-8").splitlines()
    start = next(i for i, line in enumerate(lines) if '<VST ' in line and 'Pad Trigger.vst3' in line)
    end = next(i for i in range(start, len(lines)) if lines[i].strip() == ">")
    vst, *encoded = [line.strip() for line in lines[start:end]]
    header = bytearray(b64decode(encoded[0]))
    state = b64decode("".join(encoded[1:-1]))
    size, _, tag, xml_length = struct.unpack_from("<II4sI", state)
    if tag != b"VC2!" or struct.unpack_from("<I", header, 48)[0] != len(state):
        raise ValueError("The template's VST3 state format changed.")
    xml = (
        f'<?xml version="1.0" encoding="UTF-8"?><PadTrigger samplePath={quoteattr(str(sample))}>'
        '<PARAM id="pitchSemitones" value="-24"/><PARAM id="pitchCents" value="-100"/>'
        '<PARAM id="soundMode" value="0"/><PARAM id="sampleGain" value="-6"/></PadTrigger>'
    ).encode()
    delta = len(xml) - xml_length
    # Keep REAPER's VST routing and JUCE's trailing metadata from the shipping template.
    new_state = struct.pack("<II4sI", size + delta, 1, tag, len(xml)) + xml + state[16 + xml_length:]
    struct.pack_into("<I", header, 48, len(new_state))
    chunk = ["<TRACK", "NCHAN 2", "MAINSEND 1 0", "<FXCHAIN", "BYPASS 0 0 0", vst,
             b64encode(header).decode(), *textwrap.wrap(b64encode(new_state).decode(), 128),
             encoded[-1], ">", ">", ">"]
    (folder / "fixture.chunk").write_text("\n".join(chunk) + "\n", encoding="utf-8")
    return stereo


def verify_output(path, sample):
    with wave.open(str(path), "rb") as audio:
        actual_format = audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getnframes()
        if actual_format != (2, 2, RATE, RENDER_FRAMES):
            raise ValueError("The render has unexpected channels, format, or length.")
        rendered = array("h")
        rendered.frombytes(audio.readframes(RENDER_FRAMES))
    expected_last = STRIKE + round(10 * RATE / PITCH_STEP)
    if any(rendered[:STRIKE * 2]) or any(rendered[2 * (expected_last + 1):]):
        raise ValueError("The result contains audio outside the expected one-shot.")
    gain = 10 ** (-6 / 20)
    error = 0.0
    for frame in range(STRIKE, expected_last, 16):
        position = (frame - STRIKE) * PITCH_STEP
        at = min(int(position), len(sample) // 2 - 1)
        after = min(at + 1, len(sample) // 2 - 1)
        fraction = position - int(position)
        for channel in range(2):
            value = sample[2 * at + channel] * (1 - fraction) + sample[2 * after + channel] * fraction
            error = max(error, abs(rendered[2 * frame + channel] - gain * value))
    if error > 3:
        raise ValueError(f"The rendered stereo waveform differs by up to {error:.1f} PCM steps.")
    return error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reaper", type=Path, required=True, help="Path to reaper.exe")
    parser.add_argument("--bundle", type=Path, required=True, help="Path to the Pad Trigger.vst3 folder")
    args = parser.parse_args()
    reaper, bundle = args.reaper.resolve(strict=True), args.bundle.resolve(strict=True)
    repo = Path(__file__).resolve().parents[1]
    cache = repo / "build"
    cache.mkdir(exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix="reaper-render-", dir=cache))
    print(f"Render test files in {folder}", flush=True)
    sample = make_fixture(folder, repo)
    config = folder / "reaper.ini"
    # First-launch scanning adds the default folders if they are absent. Put the
    # requested build last so it takes precedence over an installed duplicate.
    vst_paths = rf"%COMMONPROGRAMFILES%\VST3;%LOCALAPPDATA%\Programs\Common\VST3;{bundle.parent}"
    config.write_text(f"[reaper]\nloadlastproj=0\nrenderclosewhendone=4\nvstpath64={vst_paths}\n"
                      "[audioconfig]\nmode=5\n", encoding="utf-8")  # Windows Dummy Audio
    (folder / "bundle.txt").write_text(str(bundle), encoding="utf-8")
    project = folder / "fixture.rpp"
    project.write_text('<REAPER_PROJECT 0.1 "7.86/win64" 0\n>\n', encoding="ascii")
    result = folder / "result.txt"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    script = Path(__file__).resolve().with_suffix(".lua")
    command = [str(reaper), "-newinst", "-cfgfile", str(config), "-nosplash", str(project), str(script)]
    with subprocess.Popen(command, startupinfo=startup) as process:
        try:
            deadline = time.monotonic() + 90
            while not result.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.2)
            if not result.exists():
                raise RuntimeError("REAPER did not complete the render test.")
            status = result.read_text(encoding="utf-8")
            if not status.startswith("PASS "):
                raise RuntimeError(status)
            error = verify_output(folder / "processed.wav", sample)
            print(f"{status}: 42.38-second stereo tail, maximum error {error:.2f} PCM steps.")
        finally:
            if process.poll() is None:
                process.terminate()


if __name__ == "__main__":
    main()
