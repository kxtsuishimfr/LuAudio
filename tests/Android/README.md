# LuAudio Android Tests

This is a small Android application used to run native LuAudio and Oboe tests on a physical device. It is intentionally separate from Lunaris and does not use the Maven/Prefab Oboe dependency.

## Structure

- `app/src/main/java/.../MainActivity.java`: tiny bootstrap that loads the native library and displays results.
- `app/src/main/cpp/AndroidTestMain.cpp`: native test entry point and smoke test.
- `../../../CMakeLists.txt`: reuses the LuAudio, LuAudioOboe, and vendored Oboe targets.

The app passes `-DLUAUDIO_ANDROID_TESTS=ON`, which adds the `native-lib` test target without changing normal LuAudio builds.

## Build and install

Open `tests/Android` as a standalone Gradle project in Android Studio, select a connected device, and run the `app` configuration.

From the LuAudio repository root:

```text
python scripts/tests/Android/BuildInstallTests.py
```

Use `--serial SERIAL` when more than one phone is connected. Use `--no-launch` to install without starting the activity. View native output with `adb logcat -s LuAudioAndroidTests`.

The project uses API 26 as its minimum and builds `arm64-v8a`, `armeabi-v7a`, and `x86_64`. Add native test functions to `AndroidTestMain.cpp` as the backend grows; keep tests device-independent where possible and reserve Oboe stream tests for this app.