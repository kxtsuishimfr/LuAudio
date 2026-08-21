import argparse
import re
import sys
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve()
ROOT_DIR = SCRIPT_PATH.parents[3]
ANDROID_TESTS_DIR = ROOT_DIR / "tests" / "Android"
PACKAGE_NAME = "com.luaudio.androidtests"
NAME_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9_-]*$")


def jni_identifier(value: str) -> str:
    return value.replace("_", "_1").replace(".", "_")


def render_files(app_name: str, package_name: str) -> dict[str, str]:
    package_path = package_name.replace(".", "/")
    native_function = "Java_{}_MainActivity_nativeGetMessage".format(
        jni_identifier(package_name)
    )
    cmake_path = "../../../../CMakeLists.txt"

    return {
        "settings.gradle.kts": f'''import org.gradle.api.initialization.resolve.RepositoriesMode

pluginManagement {{
    repositories {{
        google()
        mavenCentral()
        gradlePluginPortal()
    }}
}}

dependencyResolutionManagement {{
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {{
        google()
        mavenCentral()
    }}
}}

rootProject.name = "{app_name}"
include(":app")
''',
        "build.gradle.kts": '''plugins {
    id("com.android.application") version "8.7.3" apply false
}
''',
        "gradle.properties": '''org.gradle.jvmargs=-Xmx2g -Dfile.encoding=UTF-8
android.useAndroidX=false
android.nonTransitiveRClass=true
''',
        "build_and_install.py": f'''import shutil
import subprocess
import sys
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve()
APP_DIR = SCRIPT_PATH.parent
PACKAGE_NAME = "{package_name}"
ACTIVITY_NAME = f"{{PACKAGE_NAME}}/.MainActivity"
APK_PATH = APP_DIR / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
ANDROID_SDK_PATH = Path(r"E:\\Android\\Sdk")
LOCAL_PROPERTIES_PATH = APP_DIR / "local.properties"


def run(command: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    print("+ " + " ".join(command), flush=True)
    return subprocess.run(command, cwd=APP_DIR, check=check, text=True)


def find_gradle() -> str:
    gradle = shutil.which("gradle")
    if gradle:
        return gradle

    cache = Path.home() / ".gradle" / "wrapper" / "dists"
    candidates = sorted(cache.glob("gradle-8.*-bin/*/gradle-8.*/bin/gradle.bat"), reverse=True)
    if candidates:
        return str(candidates[0])
    raise RuntimeError("Gradle was not found on PATH or in the local wrapper cache")


def configure_android_sdk() -> None:
    if not ANDROID_SDK_PATH.is_dir():
        raise RuntimeError(f"Android SDK was not found at {{ANDROID_SDK_PATH}}")
    LOCAL_PROPERTIES_PATH.write_text(
        f"sdk.dir={{ANDROID_SDK_PATH.as_posix()}}\\n",
        encoding="utf-8",
    )
    print(f"Android SDK: {{ANDROID_SDK_PATH}}", flush=True)


def physical_devices() -> list[str]:
    result = subprocess.run(
        ["adb", "devices"], check=True, capture_output=True, text=True
    )
    devices = []
    for line in result.stdout.splitlines()[1:]:
        fields = line.split()
        if len(fields) < 2 or fields[1] != "device":
            continue
        serial = fields[0]
        qemu = subprocess.run(
            ["adb", "-s", serial, "shell", "getprop", "ro.kernel.qemu"],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        if qemu != "1":
            devices.append(serial)
    if not devices:
        raise RuntimeError("No authorized physical Android device is connected")
    return devices


def grant_external_storage(serial: str) -> None:
    for permission in (
        "android.permission.READ_MEDIA_AUDIO",
        "android.permission.READ_EXTERNAL_STORAGE",
        "android.permission.WRITE_EXTERNAL_STORAGE",
    ):
        run(["adb", "-s", serial, "shell", "pm", "grant", PACKAGE_NAME, permission], check=False)
    run([
        "adb", "-s", serial, "shell", "appops", "set", PACKAGE_NAME,
        "MANAGE_EXTERNAL_STORAGE", "allow",
    ], check=False)


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Build, install, and launch the generated LuAudio Android test app."
    )
    parser.add_argument(
        "--no-launch",
        action="store_true",
        help="Install the APK without launching the test activity.",
    )
    args = parser.parse_args()

    try:
        gradle = find_gradle()
        configure_android_sdk()
        run([gradle, ":app:assembleDebug", "--no-daemon"], check=True)
        devices = physical_devices()
        for serial in devices:
            run(["adb", "-s", serial, "install", "-r", str(APK_PATH)])
            grant_external_storage(serial)
            if not args.no_launch:
                run([
                    "adb", "-s", serial, "shell", "am", "start", "--user", "0",
                    "-n", ACTIVITY_NAME
                ])
            print(f"Installed on {{serial}} with external storage access", flush=True)
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Error: {{error}}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
''',
        "app/build.gradle.kts": f'''plugins {{
    id("com.android.application")
}}

android {{
    namespace = "{package_name}"
    compileSdk = 36

    defaultConfig {{
        applicationId = "{package_name}"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        ndk {{
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }}

        externalNativeBuild {{
            cmake {{
                arguments += "-DANDROID_STL=c++_shared"
                arguments += "-DLUAUDIO_ANDROID_TESTS=ON"
                arguments += "-DLUAUDIO_ANDROID_TEST_SOURCE=${{file("src/main/cpp/main.cpp").absolutePath}}"
            }}
        }}
    }}

    buildTypes {{
        release {{
            isMinifyEnabled = false
        }}
    }}

    compileOptions {{
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }}

    externalNativeBuild {{
        cmake {{
            path = file("{cmake_path}")
        }}
    }}
}}
''',
        "app/src/main/AndroidManifest.xml": f'''<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-permission android:name="android.permission.MANAGE_EXTERNAL_STORAGE" />
    <uses-permission android:name="android.permission.READ_MEDIA_AUDIO" />
    <uses-permission
        android:name="android.permission.WRITE_EXTERNAL_STORAGE"
        android:maxSdkVersion="32" />
    <uses-permission
        android:name="android.permission.READ_EXTERNAL_STORAGE"
        android:maxSdkVersion="32" />
    <application
        android:label="{app_name}"
        android:theme="@android:style/Theme.Material.Light.NoActionBar">
        <activity
            android:name=".MainActivity"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
''',
        f"app/src/main/java/{package_path}/MainActivity.java": f'''package {package_name};

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

public final class MainActivity extends Activity {{
    static {{
        System.loadLibrary("native-lib");
    }}

    private static native String nativeGetMessage();

    @Override
    protected void onCreate(Bundle savedInstanceState) {{
        super.onCreate(savedInstanceState);
        TextView output = new TextView(this);
        output.setText(nativeGetMessage());
        setContentView(output);
    }}
}}
''',
        "app/src/main/cpp/main.cpp": f'''#include <jni.h>

extern "C" JNIEXPORT jstring JNICALL {native_function}(
    JNIEnv* environment,
    jclass) {{
    return environment->NewStringUTF("LuAudio Android test app is running");
}}
''',
    }


def generate(app_name: str, output_root: Path) -> Path:
    if not NAME_PATTERN.fullmatch(app_name):
        raise ValueError("app name must start with a letter and contain only letters, numbers, '_' or '-'")

    app_dir = output_root / app_name
    if app_dir.exists():
        raise FileExistsError(f"directory already exists: {app_dir}")

    for relative_path, content in render_files(app_name, PACKAGE_NAME).items():
        destination = app_dir / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(content, encoding="utf-8", newline="\n")
    return app_dir


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate an isolated Android app for LuAudio native testing."
    )
    parser.add_argument("name", help="directory and display name for the generated app")
    args = parser.parse_args()

    try:
        app_dir = generate(args.name, ANDROID_TESTS_DIR)
    except (FileExistsError, OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    print(f"Generated Android app: {app_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())