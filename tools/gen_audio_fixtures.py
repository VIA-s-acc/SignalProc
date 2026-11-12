"""Regenerate tests/fixtures/audio. Needs the `flac`, `lame` and `ffmpeg` command-line tools."""

import math
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "tests" / "fixtures" / "audio"


def write_wav(path, rate, bits, channels):
    frames = len(channels[0])
    scale = (1 << (bits - 1)) - 1
    with wave.open(str(path), "wb") as w:
        w.setnchannels(len(channels))
        w.setsampwidth(bits // 8)
        w.setframerate(rate)
        data = bytearray()
        for n in range(frames):
            for ch in channels:
                v = int(round(ch[n] * scale))
                data += struct.pack("<i", v)[: bits // 8]
        w.writeframes(bytes(data))


def tone(freq, amp, rate, frames):
    return [amp * math.sin(2 * math.pi * freq * n / rate) for n in range(frames)]


def main():
    for tool in ("flac", "lame", "ffmpeg"):
        if not shutil.which(tool):
            sys.exit(f"{tool} is not on PATH")
    OUT.mkdir(parents=True, exist_ok=True)
    stereo = OUT / "tone_stereo.wav"
    write_wav(stereo, 44100, 16, [tone(440, 0.5, 44100, 4410), tone(1000, 0.25, 44100, 4410)])
    mono24 = OUT / "tone_mono24.wav"
    write_wav(mono24, 48000, 24, [tone(250, 0.7, 48000, 2400)])
    six = OUT / "tone_6ch.wav"
    write_wav(six, 22050, 16, [tone(200 * (c + 1), 0.1 * (c + 1), 22050, 1000) for c in range(6)])
    for src in (stereo, mono24, six):
        subprocess.run(["flac", "-s", "-f", "-8", "--no-padding", "--channel-map=none", "-o", str(src.with_suffix(".flac")), str(src)], check=True)
    subprocess.run(["lame", "--quiet", "-b", "192", "--noreplaygain", str(stereo), str(OUT / "tone_stereo.mp3")], check=True)
    subprocess.run(["lame", "--quiet", "-b", "128", "-m", "m", "--noreplaygain", "--add-id3v2", "--tt", "tone", str(mono24), str(OUT / "tone_mono_id3.mp3")], check=True)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(OUT / "tone_stereo.mp3"), "-c:a", "pcm_f32le", str(OUT / "tone_stereo_mp3_ffmpeg.wav")], check=True)
    shutil.copy(OUT / "tone_stereo.flac", OUT / "flac_named.wav")
    (OUT / "not_audio.mp3").write_bytes(b"this is a text file, not audio\n")
    mono24.unlink()
    six.unlink()


if __name__ == "__main__":
    main()
