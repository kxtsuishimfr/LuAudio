package com.luaudio.androidtests;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private static final int STORAGE_PERMISSION_REQUEST = 1001;
    private TextView output;

    static {
        System.loadLibrary("native-lib");
    }

    private static native String nativeStartPlayback();
    private static native String nativeSeekMiddle();
    private static native String nativeRewind();
    private static native String nativeSeekRelative(long seconds);
    private static native String nativeSeekEnd();
    private static native String nativeStatus();
    private static native void nativeStopPlayback();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.VERTICAL);
        controls.setPadding(32, 32, 32, 32);

        output = new TextView(this);
        output.setTextSize(16.0f);
        output.setText("Preparing playback test...");
        controls.addView(output, new LinearLayout.LayoutParams(-1, 0, 1.0f));

        addButton(controls, "Seek to middle", view -> runCommand(MainActivity::nativeSeekMiddle));
        addButton(controls, "Rewind to beginning", view -> runCommand(MainActivity::nativeRewind));
        addButton(controls, "Seek backward 5 seconds", view -> runCommand(() -> nativeSeekRelative(-5)));
        addButton(controls, "Seek forward 5 seconds", view -> runCommand(() -> nativeSeekRelative(5)));
        addButton(controls, "Seek to end", view -> runCommand(MainActivity::nativeSeekEnd));
        addButton(controls, "Refresh status", view -> runCommand(MainActivity::nativeStatus));

        setContentView(controls);

        if (hasAudioPermission()) {
            startPlayback();
        } else {
            output.setText("Requesting audio file permission...");
            requestPermissions(new String[] {audioPermission()}, STORAGE_PERMISSION_REQUEST);
        }
    }

    @Override
    protected void onDestroy() {
        nativeStopPlayback();
        super.onDestroy();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != STORAGE_PERMISSION_REQUEST) {
            return;
        }
        if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            startPlayback();
        } else {
            output.setText("Audio file permission was denied.");
        }
    }

    private void startPlayback() {
        runCommand(MainActivity::nativeStartPlayback);
    }

    private void runCommand(java.util.function.Supplier<String> command) {
        output.setText(command.get());
    }

    private void addButton(LinearLayout parent, String label, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(label);
        button.setOnClickListener(listener);
        parent.addView(button, new LinearLayout.LayoutParams(-1, -2));
    }

    private boolean hasAudioPermission() {
        return checkSelfPermission(audioPermission()) == PackageManager.PERMISSION_GRANTED;
    }

    private String audioPermission() {
        return android.os.Build.VERSION.SDK_INT >= 33
            ? "android.permission.READ_MEDIA_AUDIO"
            : "android.permission.READ_EXTERNAL_STORAGE";
    }
}
