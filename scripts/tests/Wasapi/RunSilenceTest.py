from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD_DIR = PROJECT_ROOT / "out" / "build" / "wasapi-tests"


def run(command: list[str], cwd: Path) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and run the LuAudio WASAPI silence test.")
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIR,
        help="CMake build directory.",
    )
    parser.add_argument(
        "--skip-configure",
        action="store_true",
        help="Use an existing CMake build directory.",
    )
    args = parser.parse_args()

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

    run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "LuAudioWasapiSilenceTest",
        ],
        PROJECT_ROOT,
    )

    executable = build_dir / "LuAudioWasapiSilenceTest.exe"
    if not executable.exists():
        executable = build_dir / "LuAudioWasapiSilenceTest"
    if not executable.exists():
        print(f"Test executable was not found in {build_dir}", file=sys.stderr)
        return 1

    run([str(executable)], PROJECT_ROOT)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except FileNotFoundError as error:
        print(f"Required command was not found: {error.filename}", file=sys.stderr)
        raise SystemExit(1) from error
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from error
