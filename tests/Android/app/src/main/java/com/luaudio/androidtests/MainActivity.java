package com.luaudio.androidtests;

import android.app.Activity;
import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;

public final class MainActivity extends Activity {
    private static final int AUDIO_PERMISSION_REQUEST = 1001;

    static {
        System.loadLibrary("native-lib");
    }

    private TextView output;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.VERTICAL);
        controls.setPadding(32, 32, 32, 32);

        output = new TextView(this);
        output.setTextSize(16.0f);
        output.setText("Android playback is ready.");
        controls.addView(output, new LinearLayout.LayoutParams(-1, 0, 1.0f));

        File testDirectory = new File(Environment.getExternalStorageDirectory(), "LuAudio_Tests");
        addButton(controls, "Request audio permission", this::requestAudioPermission);
        addButton(controls, "Start WAV + MP3 mixer", () -> runCommand(
            () -> nativeRunAudioMixerPlaybackTest(testDirectory.getAbsolutePath())));
        addButton(controls, "Stop mixer", () -> runCommand(MainActivity::nativeStopAudioMixer));
        addButton(controls, "Pause/resume WAV", () -> {
            wavPaused = !wavPaused;
            runCommand(() -> nativeSetWavPaused(wavPaused));
        });
        addButton(controls, "Pause/resume MP3", () -> {
            mp3Paused = !mp3Paused;
            runCommand(() -> nativeSetMp3Paused(mp3Paused));
        });
        addButton(controls, "WAV seek -5s", () -> runCommand(() -> nativeSeekWav(false)));
        addButton(controls, "WAV seek +5s", () -> runCommand(() -> nativeSeekWav(true)));
        addButton(controls, "MP3 seek -5s", () -> runCommand(() -> nativeSeekMp3(false)));
        addButton(controls, "MP3 seek +5s", () -> runCommand(() -> nativeSeekMp3(true)));
        addButton(controls, "Stop WAV source", () -> runCommand(MainActivity::nativeStopWav));
        addButton(controls, "Stop MP3 source", () -> runCommand(MainActivity::nativeStopMp3));
        setContentView(controls);
    }

    private static native String nativeRunAudioMixerPlaybackTest(String directory);
    private static native String nativeStopAudioMixer();
    private static native String nativeSetWavPaused(boolean paused);
    private static native String nativeSetMp3Paused(boolean paused);
    private static native String nativeSeekWav(boolean forward);
    private static native String nativeSeekMp3(boolean forward);
    private static native String nativeStopWav();
    private static native String nativeStopMp3();

    private boolean wavPaused;
    private boolean mp3Paused;

    private void requestAudioPermission() {
        String permission = Build.VERSION.SDK_INT >= 33
            ? Manifest.permission.READ_MEDIA_AUDIO
            : Manifest.permission.READ_EXTERNAL_STORAGE;
        if (checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED) {
            output.setText("Audio permission granted.");
        } else {
            requestPermissions(new String[] {permission}, AUDIO_PERMISSION_REQUEST);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == AUDIO_PERMISSION_REQUEST && grantResults.length > 0 &&
            grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            output.setText("Audio permission granted.");
        } else if (requestCode == AUDIO_PERMISSION_REQUEST) {
            output.setText("Audio permission denied.");
        }
    }

    private void runCommand(java.util.function.Supplier<String> command) {
        output.setText("Working...");
        new Thread(() -> {
            String result = command.get();
            runOnUiThread(() -> output.setText(result));
        }, "LuAudioAndroidCommand").start();
    }

    private void addButton(LinearLayout parent, String label, Runnable command) {
        Button button = new Button(this);
        button.setText(label);
        button.setOnClickListener(view -> command.run());
        parent.addView(button, new LinearLayout.LayoutParams(-1, -2));
    }
}
