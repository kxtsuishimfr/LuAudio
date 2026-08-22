import ctypes
import subprocess
import sys
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve()
ROOT_DIR = SCRIPT_PATH.parents[3]
BUILD_DIR = ROOT_DIR / "out" / "build" / "x64-debug"
TARGET_NAME = "LuAudioOrderSensitiveEffectOrderTest"
VS_DEV_CMD = Path(
    r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
)


def run(command: str) -> None:
    print(f"+ {command}")
    shell_command = f"call {windows_short_path(VS_DEV_CMD)} -arch=x64 && {command}"
    subprocess.run(
        ["cmd.exe", "/d", "/c", shell_command],
        cwd=ROOT_DIR,
        check=True,
    )


def windows_short_path(path: Path) -> str:
    buffer = ctypes.create_unicode_buffer(32768)
    length = ctypes.windll.kernel32.GetShortPathNameW(
        str(path), buffer, len(buffer)
    )
    if length == 0:
        raise OSError(f"Unable to resolve a short Windows path for {path}")
    return buffer.value


def main() -> int:
    if not VS_DEV_CMD.is_file():
        print(f"Visual Studio developer command file was not found: {VS_DEV_CMD}", file=sys.stderr)
        return 1

    try:
        run("cmake --preset x64-debug")
        run(f"cmake --build {BUILD_DIR} --target {TARGET_NAME} --parallel 4")
        run(f"{BUILD_DIR / TARGET_NAME}.exe")
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    print(f"{TARGET_NAME} exited successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
