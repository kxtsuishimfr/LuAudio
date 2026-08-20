package com.luaudio.androidtests;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private static final int STORAGE_PERMISSION_REQUEST = 1001;
    private TextView output;

    static {
        System.loadLibrary("native-lib");
    }

    private static native String runPlaybackTest();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        output = new TextView(this);
        output.setTextSize(16.0f);
        output.setPadding(32, 32, 32, 32);
        setContentView(output);

        if (hasAudioPermission()) {
            startPlaybackTest();
        } else {
            output.setText("Requesting audio file permission...");
            requestPermissions(new String[] {audioPermission()}, STORAGE_PERMISSION_REQUEST);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != STORAGE_PERMISSION_REQUEST) {
            return;
        }
        if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            startPlaybackTest();
        } else {
            output.setText("Audio file permission was denied.");
        }
    }

    private boolean hasAudioPermission() {
        return checkSelfPermission(audioPermission()) == PackageManager.PERMISSION_GRANTED;
    }

    private String audioPermission() {
        return android.os.Build.VERSION.SDK_INT >= 33
            ? "android.permission.READ_MEDIA_AUDIO"
            : "android.permission.READ_EXTERNAL_STORAGE";
    }

    private void startPlaybackTest() {
        output.setText("Opening /sdcard/LuAudio_Tests/sample_1.wav...\n");
        new Thread(() -> {
            String result = runPlaybackTest();
            runOnUiThread(() -> output.setText(result));
        }, "LuAudioPlaybackTest").start();
    }
}