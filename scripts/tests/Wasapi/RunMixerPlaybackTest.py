from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD_DIR = PROJECT_ROOT / "out" / "build" / "wasapi-tests"
DEFAULT_WAV_FILE = PROJECT_ROOT / "tests" / "Audios" / "sample_1.wav"
DEFAULT_MP3_FILE = PROJECT_ROOT / "tests" / "Audios" / "sample_2.mp3"
TARGET_NAME = "LuAudioWasapiMixerPlaybackTest"


def run(command: list[str], cwd: Path) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build and run the interactive LuAudio WASAPI mixer playback test."
    )
    parser.add_argument("wav_file", nargs="?", type=Path, default=DEFAULT_WAV_FILE)
    parser.add_argument("mp3_file", nargs="?", type=Path, default=DEFAULT_MP3_FILE)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--skip-configure", action="store_true")
    args = parser.parse_args()

    wav_file = args.wav_file.resolve()
    mp3_file = args.mp3_file.resolve()
    if not wav_file.is_file():
        print(f"WAV file was not found: {wav_file}", file=sys.stderr)
        return 1
    if not mp3_file.is_file():
        print(f"MP3 file was not found: {mp3_file}", file=sys.stderr)
        return 1

    build_dir = args.build_dir.resolve()
    build_dir.parent.mkdir(parents=True, exist_ok=True)

    if not args.skip_configure:
        run(
            [
                "cmake",
                "-S",
                str(PROJECT_ROOT),
                "-B",
                str(build_dir),
                "-G",
                "Ninja",
                "-DBUILD_TESTING=ON",
                "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=",
            ],
            PROJECT_ROOT,
        )

    run(["cmake", "--build", str(build_dir), "--target", TARGET_NAME], PROJECT_ROOT)

    executable = build_dir / f"{TARGET_NAME}.exe"
    if not executable.exists():
        executable = build_dir / TARGET_NAME
    if not executable.exists():
        print(f"Playback executable was not found in {build_dir}", file=sys.stderr)
        return 1

    print("W: stop WAV, S: stop MP3, E: pause/resume WAV, R: pause/resume MP3")
    print("Left/Right: seek active sources by 5 seconds, Q/Escape: quit")
    run([str(executable), str(wav_file), str(mp3_file)], PROJECT_ROOT)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except FileNotFoundError as error:
        print(f"Required command was not found: {error.filename}", file=sys.stderr)
        raise SystemExit(1) from error
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from error
