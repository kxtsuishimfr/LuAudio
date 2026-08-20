package com.luaudio.androidtests;

import android.app.Activity;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    static {
        System.loadLibrary("native-lib");
        System.loadLibrary("LuAudioTestPlugin");
    }

    private static native String nativeStartPlayback();
    private static native String nativeToggleReverb(boolean enabled);
    private static native String nativeTogglePause();
    private static native String nativeSeek(long frame);
    private static native String nativeSeekRelative(long seconds);
    private static native String nativeStatus();
    private static native String nativeRunPluginTests(String pluginPath);
    private static native void nativeStopPlayback();

    private TextView output;
    private boolean reverbEnabled = true;

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

        addButton(controls, "Start sample_1.wav", () -> runCommand(MainActivity::nativeStartPlayback));
        addButton(controls, "Pause / resume", () -> runCommand(MainActivity::nativeTogglePause));
        addButton(controls, "Toggle reverb", () -> {
            reverbEnabled = !reverbEnabled;
            runCommand(() -> nativeToggleReverb(reverbEnabled));
        });
        addButton(controls, "Seek middle", () -> runCommand(() -> nativeSeek(-1)));
        addButton(controls, "Seek backward 5 seconds", () -> runCommand(() -> nativeSeekRelative(-5)));
        addButton(controls, "Seek forward 5 seconds", () -> runCommand(() -> nativeSeekRelative(5)));
        addButton(controls, "Seek end", () -> runCommand(() -> nativeSeek(-2)));
        addButton(controls, "Refresh status", () -> runCommand(MainActivity::nativeStatus));
        addButton(controls, "Run plugin test", () -> runCommand(
            () -> nativeRunPluginTests("libLuAudioTestPlugin.so")));

        setContentView(controls);
    }

    @Override
    protected void onDestroy() {
        nativeStopPlayback();
        super.onDestroy();
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
