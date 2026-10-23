package org.sdl2.perfectdark;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.ByteArrayOutputStream;
import android.graphics.Bitmap;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.InputDevice;

/** Optional native smoke test. Needs a user-supplied ROM and the wiki's G5 example. */
public class ModGameRunner extends Instrumentation {
    private boolean useMod;
    private int stage;
    private String sequence;
    private String fixture;
    private boolean testReload;
    @Override public void onCreate(Bundle args) {
        super.onCreate(args);
        useMod = args != null && "true".equals(args.getString("mod"));
        stage = args == null ? 0x1d : Integer.decode(args.getString("stage", "0x1d"));
        sequence = args == null ? null : args.getString("sequence");
        fixture = args == null ? "real" : args.getString("fixture", "real");
        testReload = args != null && "true".equals(args.getString("reload"));
        start();
    }

    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            Context context = getTargetContext();
            if (!new File(context.getFilesDir(), "pd.ntsc-final.z64").isFile()) {
                throw new IllegalStateException("Supply your own ROM before running the game smoke test.");
            }
            if (useMod) {
                ModStorage.importTree(context, DocumentsContract.buildTreeDocumentUri(
                        "org.sdl2.perfectdark.test.documents", fixture));
            } else if (!ModStorage.disable(context)) {
                throw new IllegalStateException("Could not clear the test mod selection.");
            }
            Intent game = new Intent(context, SDLActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            File active = ModStorage.activeDirectory(context);
            if (active != null) game.putExtra("mod_directory", active.getName());
            // Chicago is replaced by the G5 example. Skip menu navigation in this smoke test.
            game.putExtra("test_boot_stage", stage);
            context.startActivity(game);
            // Keep instrumentation alive while the separate game process boots.
            Thread.sleep(30000);
            ByteArrayOutputStream text = new ByteArrayOutputStream();
            try (FileInputStream in = new FileInputStream(new File(context.getFilesDir(), "pd.log"))) {
                byte[] buffer = new byte[4096];
                int count;
                while ((count = in.read(buffer)) != -1) text.write(buffer, 0, count);
            }
            String log = text.toString("UTF-8");
            if (!log.contains("romdataInit: loaded rom") || !log.contains("touch: surface")) {
                throw new AssertionError("Game did not reach the first rendered frame; inspect pd.log.");
            }
            if (useMod && sequence == null && !log.contains("(bgdata/bg_mp5.seg) loaded externally")) {
                throw new AssertionError("G5 background was not loaded from the imported mod.");
            }
            if (useMod && sequence != null && !log.contains("mod: loaded external sequence " + sequence)) {
                throw new AssertionError("External music sequence " + sequence + " was not loaded.\n" + log);
            }
            if (!useMod && log.contains(" mod dir:")) {
                throw new AssertionError("Disabled mod is still active in the native engine.");
            }
            Bitmap frame = getUiAutomation().takeScreenshot();
            if (frame == null) throw new AssertionError("No screen capture available.");
            if (testReload) {
                android.view.accessibility.AccessibilityNodeInfo window = getUiAutomation().getRootInActiveWindow();
                if (window == null || !context.getPackageName().contentEquals(window.getPackageName())) {
                    throw new AssertionError("Game is not the foreground window; refusing touch injection.");
                }
                saveFrame(context, "reload-before.png");
                float unit = Math.min(frame.getWidth(), frame.getHeight()) * .085f;
                hold(frame.getWidth() - 1.75f * unit, frame.getHeight() - 1.75f * unit, 1000);
                Thread.sleep(5000);
                saveFrame(context, "reload-fired.png");
                hold(frame.getWidth() - 5.35f * unit, frame.getHeight() - 5.35f * unit, 500);
                Thread.sleep(7000);
                saveFrame(context, "reload-after.png");
            }
            try (FileOutputStream out = new FileOutputStream(new File(context.getFilesDir(), "smoke-frame.png"))) {
                frame.compress(Bitmap.CompressFormat.PNG, 100, out);
            }
            frame.recycle();
            result.putString("stream", "PASS: ROM loaded, first frame rendered, "
                    + (useMod ? (sequence == null ? "G5 background loaded externally" : "music sequence " + sequence + " loaded externally") : "mod disabled in native engine")
                    + "; screenshot saved.\n");
            finish(Activity.RESULT_OK, result);
        } catch (Throwable error) {
            result.putString("stream", android.util.Log.getStackTraceString(error));
            finish(Activity.RESULT_CANCELED, result);
        }
    }

    private void hold(float x, float y, long duration) throws Exception {
        long start = SystemClock.uptimeMillis();
        MotionEvent down = MotionEvent.obtain(start, start, MotionEvent.ACTION_DOWN, x, y, 0);
        down.setSource(InputDevice.SOURCE_TOUCHSCREEN);
        if (!getUiAutomation().injectInputEvent(down, true)) throw new AssertionError("Touch down rejected");
        down.recycle();
        Thread.sleep(duration);
        MotionEvent up = MotionEvent.obtain(start, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP, x, y, 0);
        up.setSource(InputDevice.SOURCE_TOUCHSCREEN);
        if (!getUiAutomation().injectInputEvent(up, true)) throw new AssertionError("Touch up rejected");
        up.recycle();
    }

    private void saveFrame(Context context, String name) throws Exception {
        Bitmap frame = getUiAutomation().takeScreenshot();
        if (frame == null) throw new AssertionError("No screenshot for " + name);
        try (FileOutputStream out = new FileOutputStream(new File(context.getFilesDir(), name))) {
            frame.compress(Bitmap.CompressFormat.PNG, 100, out);
        }
        frame.recycle();
    }
}
