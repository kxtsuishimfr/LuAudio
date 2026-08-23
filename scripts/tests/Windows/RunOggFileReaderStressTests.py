import argparse
import ctypes
import subprocess
import sys
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve()
ROOT_DIR = SCRIPT_PATH.parents[3]
DEFAULT_BUILD_DIR = ROOT_DIR / "out" / "build" / "x64-debug"
TARGET_NAME = "LuAudioOggFileReaderStressTests"
TEST_FILTER = "OggFileReaderStressTests"
VS_DEV_CMD = Path(
    r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
)


def windows_short_path(path: Path) -> str:
    buffer = ctypes.create_unicode_buffer(32768)
    length = ctypes.windll.kernel32.GetShortPathNameW(
        str(path), buffer, len(buffer)
    )
    if length == 0:
        raise OSError(f"Unable to resolve a short Windows path for {path}")
    return buffer.value


def run(command: str, root_dir: Path) -> None:
    print(f"+ {command}")
    shell_command = f"call {windows_short_path(VS_DEV_CMD)} -arch=x64 && {command}"
    subprocess.run(
        ["cmd.exe", "/d", "/c", shell_command],
        cwd=root_dir,
        check=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build and run LuAudio OggFileReader stress tests."
    )
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

    if not VS_DEV_CMD.is_file():
        print(
            f"Visual Studio developer command file was not found: {VS_DEV_CMD}",
            file=sys.stderr,
        )
        return 1

    build_dir = args.build_dir.resolve()
    try:
        if not args.skip_configure:
            run(
                f"cmake -S {ROOT_DIR} -B {build_dir} -G Ninja "
                "-DBUILD_TESTING=ON -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=",
                ROOT_DIR,
            )
        run(
            f"cmake --build {build_dir} --target {TARGET_NAME} --parallel 4",
            ROOT_DIR,
        )
        run(
            f"ctest --test-dir {build_dir} -R {TEST_FILTER} "
            "--output-on-failure --no-tests=error",
            ROOT_DIR,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    print("LuAudio OggFileReader stress tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
