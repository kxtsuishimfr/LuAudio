from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD_DIR = PROJECT_ROOT / "out" / "build" / "wasapi-tests"
DEFAULT_AUDIO_FILE = PROJECT_ROOT / "tests" / "Audios" / "sample_1.wav"
TARGET_NAME = "LuAudioWasapiSeekPlaybackTest"


def run(command: list[str], cwd: Path) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build and run the interactive LuAudio WASAPI seek playback test."
    )
    parser.add_argument("audio_file", nargs="?", type=Path, default=DEFAULT_AUDIO_FILE)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--skip-configure", action="store_true")
    args = parser.parse_args()

    audio_file = args.audio_file.resolve()
    if not audio_file.is_file():
        print(f"Audio file was not found: {audio_file}", file=sys.stderr)
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

    print("Space seeks to the middle; Alt+Space rewinds to the beginning.")
    run([str(executable), str(audio_file)], PROJECT_ROOT)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except FileNotFoundError as error:
        print(f"Required command was not found: {error.filename}", file=sys.stderr)
        raise SystemExit(1) from error
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from error
