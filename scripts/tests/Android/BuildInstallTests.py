import argparse
import shutil
import subprocess
import sys
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve()
ROOT_DIR = SCRIPT_PATH.parents[3]
ANDROID_PROJECT_DIR = ROOT_DIR / "tests" / "Android"
APK_PATH = ANDROID_PROJECT_DIR / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
DEVICE_AUDIO_DIR = "/sdcard/LuAudio_Tests"
TEST_AUDIO_FILES = ("sample_1.wav", "sample_2.mp3")
LOCAL_PROPERTIES_PATH = ANDROID_PROJECT_DIR / "local.properties"
ANDROID_SDK_PATH = Path(r"E:\Android\Sdk")
PACKAGE_NAME = "com.luaudio.androidtests"
ACTIVITY_NAME = f"{PACKAGE_NAME}/.MainActivity"


def run(command: list[str], cwd: Path | None = None) -> None:
    print(f"+ {' '.join(command)}")
    subprocess.run(command, cwd=cwd, check=True)


def find_gradle() -> str:
    gradle = shutil.which("gradle")
    if gradle:
        return gradle

    gradle_root = Path(r"C:\Users\Katsu\.gradle\wrapper\dists")
    candidates = sorted(
        gradle_root.glob("gradle-8.*-bin/*/gradle-8.*/bin/gradle.bat"),
        reverse=True,
    )
    if candidates:
        return str(candidates[0])

    raise RuntimeError("Gradle was not found on PATH or in the local Gradle wrapper cache.")


def configure_android_sdk() -> None:
    if not ANDROID_SDK_PATH.is_dir():
        raise RuntimeError(f"Android SDK was not found at {ANDROID_SDK_PATH}")

    LOCAL_PROPERTIES_PATH.write_text(
        f"sdk.dir={ANDROID_SDK_PATH.as_posix()}\n",
        encoding="utf-8",
    )
    print(f"Android SDK: {ANDROID_SDK_PATH}")


def push_test_audio(serial: str | None) -> None:
    mkdir_command = ["adb"]
    if serial:
        mkdir_command.extend(["-s", serial])
    mkdir_command.extend(["shell", "mkdir", "-p", DEVICE_AUDIO_DIR])
    run(mkdir_command)

    for file_name in TEST_AUDIO_FILES:
        source_path = ROOT_DIR / "tests" / "Audios" / file_name
        if not source_path.is_file():
            raise RuntimeError(f"Test audio was not found: {source_path}")

        push_command = ["adb"]
        if serial:
            push_command.extend(["-s", serial])
        push_command.extend(["push", str(source_path), f"{DEVICE_AUDIO_DIR}/{file_name}"])
        run(push_command)


def grant_storage_access(serial: str | None) -> None:
    for permission in (
        "android.permission.READ_MEDIA_AUDIO",
        "android.permission.READ_EXTERNAL_STORAGE",
        "android.permission.WRITE_EXTERNAL_STORAGE",
    ):
        command = ["adb"]
        if serial:
            command.extend(["-s", serial])
        command.extend(["shell", "pm", "grant", PACKAGE_NAME, permission])
        result = subprocess.run(command, cwd=ROOT_DIR)
        if result.returncode != 0:
            print(f"Permission not grantable on this Android version: {permission}")

    appops_command = ["adb"]
    if serial:
        appops_command.extend(["-s", serial])
    appops_command.extend(
        ["shell", "appops", "set", PACKAGE_NAME, "MANAGE_EXTERNAL_STORAGE", "allow"]
    )
    run(appops_command)


def find_device(requested_serial: str | None) -> str | None:
    result = subprocess.run(
        ["adb", "devices"],
        check=True,
        capture_output=True,
        text=True,
    )
    devices = []
    for line in result.stdout.splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 2 and fields[1] == "device":
            devices.append(fields[0])

    if requested_serial:
        if requested_serial not in devices:
            raise RuntimeError(f"ADB device is not connected or authorized: {requested_serial}")
        return requested_serial

    if not devices:
        raise RuntimeError("No authorized Android device found. Check `adb devices` and USB debugging.")
    if len(devices) > 1:
        raise RuntimeError(f"Multiple devices found; specify one with --serial: {', '.join(devices)}")
    return devices[0]


def main() -> int:
    parser = argparse.ArgumentParser(description="Build, install, and launch LuAudio Android tests.")
    parser.add_argument("--serial", help="ADB device serial when more than one device is connected.")
    parser.add_argument("--no-launch", action="store_true", help="Install the APK without launching the test activity.")
    args = parser.parse_args()

    try:
        gradle = find_gradle()
        serial = find_device(args.serial)
        configure_android_sdk()
        run([gradle, ":app:assembleDebug", "--no-daemon"], cwd=ANDROID_PROJECT_DIR)

        install_command = ["adb"]
        if serial:
            install_command.extend(["-s", serial])
        install_command.extend(["install", "-r", str(APK_PATH)])
        run(install_command)
        grant_storage_access(serial)
        push_test_audio(serial)

        if not args.no_launch:
            launch_command = ["adb"]
            if serial:
                launch_command.extend(["-s", serial])
            launch_command.extend(["shell", "am", "start", "-n", ACTIVITY_NAME])
            run(launch_command)

        print(f"Installed: {APK_PATH}")
        print(f"Package: {PACKAGE_NAME}")
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())