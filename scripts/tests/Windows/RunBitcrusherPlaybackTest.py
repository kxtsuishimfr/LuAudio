import ctypes
import subprocess
import sys
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve()
PROJECT_ROOT = SCRIPT_PATH.parents[3]
BUILD_DIR = PROJECT_ROOT / "out" / "build" / "x64-debug"
AUDIO_FILE = PROJECT_ROOT / "tests" / "Audios" / "sample_3.mp3"
TARGET_NAME = "LuAudioBitcrusherPlaybackTest"
VS_DEV_CMD = Path(
    r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
)


def windows_short_path(path: Path) -> str:
    buffer = ctypes.create_unicode_buffer(32768)
    length = ctypes.windll.kernel32.GetShortPathNameW(
        str(path), buffer, len(buffer)
    )
    if length == 0:
        raise OSError(f"Unable to resolve Visual Studio developer command path: {path}")
    return buffer.value


def run(command: str) -> None:
    print(f"+ {command}")
    shell_command = f"call {windows_short_path(VS_DEV_CMD)} -arch=x64 && {command}"
    subprocess.run(
        ["cmd.exe", "/d", "/c", shell_command],
        cwd=PROJECT_ROOT,
        check=True,
    )


def main() -> int:
    if not VS_DEV_CMD.is_file():
        print(f"Visual Studio developer command file was not found: {VS_DEV_CMD}", file=sys.stderr)
        return 1
    if not AUDIO_FILE.is_file():
        print(f"Audio file was not found: {AUDIO_FILE}", file=sys.stderr)
        return 1

    try:
        run("cmake --preset x64-debug")
        run(f"cmake --build {BUILD_DIR} --target {TARGET_NAME} --parallel 4")
        executable = BUILD_DIR / f"{TARGET_NAME}.exe"
        if not executable.is_file():
            print(f"Playback executable was not found: {executable}", file=sys.stderr)
            return 1
        print("Up/Down changes bit depth; Left/Right changes sample hold; D/F changes drive; Space bypasses; Q exits.")
        subprocess.run([str(executable), str(AUDIO_FILE)], cwd=PROJECT_ROOT, check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())