from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD_DIR = PROJECT_ROOT / "out" / "build" / "wasapi-tests"
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "tests" / "Audios" / "Output"
TARGET_NAME = "LuAudioExportAudioTest"
DEFAULT_WAV_FILE = PROJECT_ROOT / "tests" / "Audios" / "sample_1.wav"
DEFAULT_MP3_FILE = PROJECT_ROOT / "tests" / "Audios" / "sample_2.mp3"
DEFAULT_SAMPLE_3_FILE = PROJECT_ROOT / "tests" / "Audios" / "sample_3.mp3"


def run(command: list[str], cwd: Path) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build and run the interactive LuAudio WASAPI export test."
    )
    parser.add_argument(
        "wav_file",
        nargs="?",
        type=Path,
        default=DEFAULT_WAV_FILE,
        help="WAV input file (default: sample_1.wav).",
    )
    parser.add_argument(
        "mp3_file",
        nargs="?",
        type=Path,
        default=DEFAULT_MP3_FILE,
        help="MP3 input file (default: sample_2.mp3).",
    )
    parser.add_argument(
        "sample_3_file",
        nargs="?",
        type=Path,
        default=DEFAULT_SAMPLE_3_FILE,
        help="Third audio input file (default: sample_3.mp3).",
    )
    parser.add_argument("output_dir", nargs="?", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--skip-configure", action="store_true")
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()

    wav_file = args.wav_file.resolve()
    mp3_file = args.mp3_file.resolve()
    sample_3_file = args.sample_3_file.resolve()
    for audio_file in (wav_file, mp3_file, sample_3_file):
        if not audio_file.is_file():
            print(f"Audio file was not found: {audio_file}", file=sys.stderr)
            return 1

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
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

    if args.build_only:
        return 0

    executable = build_dir / f"{TARGET_NAME}.exe"
    if not executable.exists():
        executable = build_dir / TARGET_NAME
    if not executable.exists():
        print(f"Export executable was not found in {build_dir}", file=sys.stderr)
        return 1

    command = [str(executable)]
    command.extend([str(wav_file), str(mp3_file), str(sample_3_file), str(output_dir)])
    run(command, PROJECT_ROOT)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except FileNotFoundError as error:
        print(f"Required command was not found: {error.filename}", file=sys.stderr)
        raise SystemExit(1) from error
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from error
