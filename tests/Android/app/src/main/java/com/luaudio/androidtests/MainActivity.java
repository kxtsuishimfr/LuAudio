package com.luaudio.androidtests;

import android.app.Activity;
import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;

public final class MainActivity extends Activity {
    private static final int AUDIO_PERMISSION_REQUEST = 1001;

    static {
        System.loadLibrary("native-lib");
    }

    private TextView output;
    private final Handler statusHandler = new Handler(Looper.getMainLooper());
    private boolean exportPaused;
    private Button pauseButton;
    private Button playButton;
    private Button wavReverbButton;
    private Button mp3ReverbButton;
    private boolean wavReverb = true;
    private boolean mp3Reverb = true;
    private final Runnable exportStatusPoll = new Runnable() {
        @Override
        public void run() {
            String status = nativeGetExportStatus();
            output.setText(status);
            if (status.equals("Export started") || status.startsWith("Exporting")) {
                statusHandler.postDelayed(this, 500);
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ScrollView scroll = new ScrollView(this);
        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.VERTICAL);
        controls.setPadding(32, 32, 32, 32);

        output = new TextView(this);
        output.setTextSize(16.0f);
        output.setText("Android playback is ready.");
        controls.addView(output, new LinearLayout.LayoutParams(-1, -2));

        File testDirectory = new File(Environment.getExternalStorageDirectory(), "LuAudio_Tests");
        File outputDirectory = new File(Environment.getExternalStorageDirectory(), "LuAudio_Tests/Output");
        String pluginPath = "libProfessionalHallReverb.so";
        addButton(controls, "Request audio permission", this::requestAudioPermission);
        wavReverbButton = addButton(controls, "WAV reverb: ON", () -> {
            wavReverb = !wavReverb;
            final boolean enabled = wavReverb;
            runCommand(() -> {
                String result = nativeSetWavReverb(enabled);
                if (!result.startsWith("PASS:")) wavReverb = !enabled;
                return result;
            });
            wavReverbButton.setText("WAV reverb: " + (wavReverb ? "ON" : "OFF"));
        });
        mp3ReverbButton = addButton(controls, "MP3 reverb: ON", () -> {
            mp3Reverb = !mp3Reverb;
            final boolean enabled = mp3Reverb;
            runCommand(() -> {
                String result = nativeSetMp3Reverb(enabled);
                if (!result.startsWith("PASS:")) mp3Reverb = !enabled;
                return result;
            });
            mp3ReverbButton.setText("MP3 reverb: " + (mp3Reverb ? "ON" : "OFF"));
        });
        playButton = addButton(controls, "Play audio", () -> runCommand(() -> {
            String result = nativeStartPlayback(
                testDirectory.getAbsolutePath(), pluginPath, wavReverb, mp3Reverb);
            if (result.startsWith("PASS:")) {
                runOnUiThread(() -> {
                    pauseButton.setEnabled(true);
                    playButton.setEnabled(false);
                });
            }
            return result;
        }));
        pauseButton = addButton(controls, "Pause audio", () -> {
            exportPaused = !exportPaused;
            pauseButton.setText(exportPaused ? "Resume audio" : "Pause audio");
            runCommand(() -> nativeSetPlaybackPaused(exportPaused));
        });
        pauseButton.setEnabled(false);
        addButton(controls, "Export both samples", () -> {
            outputDirectory.mkdirs();
            runCommand(() -> {
                String result = nativeStartOfflineExport(
                    new File(outputDirectory, "sample_1_plus_sample_2_hall_reverb.wav").getAbsolutePath());
                statusHandler.post(exportStatusPoll);
                return result;
            });
        });
        addButton(controls, "Stop audio", () -> runCommand(() -> {
            String result = nativeStopPlayback();
            runOnUiThread(() -> {
                pauseButton.setEnabled(false);
                pauseButton.setText("Pause audio");
                exportPaused = false;
                playButton.setEnabled(true);
            });
            return result;
        }));
        scroll.addView(controls);
        setContentView(scroll);
    }

    private static native String nativeStartPlayback(String directory, String pluginPath,
        boolean wavReverb, boolean mp3Reverb);
    private static native String nativeSetPlaybackPaused(boolean paused);
    private static native String nativeSetWavReverb(boolean enabled);
    private static native String nativeSetMp3Reverb(boolean enabled);
    private static native String nativeStartOfflineExport(String outputPath);
    private static native String nativeStopPlayback();
    private static native String nativeGetExportStatus();

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

    private Button addButton(LinearLayout parent, String label, Runnable command) {
        Button button = new Button(this);
        button.setText(label);
        button.setOnClickListener(view -> command.run());
        parent.addView(button, new LinearLayout.LayoutParams(-1, -2));
        return button;
    }
}
