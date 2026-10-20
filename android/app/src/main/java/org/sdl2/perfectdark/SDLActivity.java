package org.sdl2.perfectdark;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;
import android.view.View;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Perfect Dark Android entry point.
 *
 * SDL2's official org.libsdl.app.SDLActivity does the work: it creates the
 * surface, hands it to SDL's video driver, and starts the SDLMain thread that
 * calls the port's SDL_main (renamed from main() via SDL_main.h on Android).
 *
 * This subclass points SDL at our shared library, adds the immersive
 * fullscreen handling, and asks the player for their own ROM the first time
 * the game starts.
 */
public class SDLActivity extends org.libsdl.app.SDLActivity {

    private static final String TAG = "PerfectDark";

    /** Request code for the ROM file picker. */
    private static final int REQUEST_PICK_ROM = 0x5044;

    /** The file name the picker result should be stored under. */
    private static String mPendingRomName;

    /** Set while the file picker is up. */
    private static boolean mPickingRom;

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "main"
        };
    }

    @Override
    protected String getMainSharedObject() {
        // The port is built as libmain.so (see CMakeLists.txt OUTPUT_NAME).
        return getContext().getApplicationInfo().nativeLibraryDir + "/libmain.so";
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }

    @Override
    protected String[] getArguments() {
        return new String[0];
    }

    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        forceLandscape();
        applyImmersiveMode();
        // Let the native side find this activity, so it can ask for the ROM.
        nativeSetActivity(this);
    }

    /**
     * SDL locks the orientation itself when it creates the window, and it
     * decides that from the window size and the SDL_HINT_ORIENTATIONS hint.
     * On a phone that can end up following the system's auto-rotate setting,
     * so the orientation is forced here instead.
     */
    private void forceLandscape() {
        setRequestedOrientation(
                android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    /**
     * SDL calls this when it sets the orientation. Ignore it and stay in
     * landscape.
     */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        forceLandscape();
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        forceLandscape();
    }

    /**
     * Hide the status and navigation bars and let the game use the whole
     * screen. The bars are re-hidden whenever the focus comes back, because
     * Android shows them again after certain system events.
     */
    private void applyImmersiveMode() {
        View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            android.view.WindowInsetsController c =
                    getWindow().getInsetsController();
            if (c != null) {
                c.hide(android.view.WindowInsets.Type.statusBars()
                        | android.view.WindowInsets.Type.navigationBars());
                c.setSystemBarsBehavior(
                        android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
        }
    }

    /**
     * The system back button is mapped onto the game's own "back" action
     * (Escape) rather than closing the activity straight away.
     */
    @Override
    public void onBackPressed() {
        nativeBackPressed();
    }

    /** Called from native code when the game did not handle a back press. */
    public void exitFromNative() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                finish();
            }
        });
    }

    /* ---------------------------------------------------------------- */
    /* ROM picking                                                       */
    /* ---------------------------------------------------------------- */

    /**
     * Shows the system file picker so the player can choose their own ROM.
     * Called from native code, which waits for onRomPickResult().
     */
    public void pickRom(final String filename) {
        mPendingRomName = filename;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (mPickingRom) {
                    // The picker is already up.
                    return;
                }
                mPickingRom = true;
                try {
                    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                    intent.addCategory(Intent.CATEGORY_OPENABLE);
                    intent.setType("*/*");
                    startActivityForResult(intent, REQUEST_PICK_ROM);
                } catch (Exception e) {
                    Log.e(TAG, "could not open the file picker: " + e);
                    mPickingRom = false;
                    onRomPickResult(false);
                }
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode != REQUEST_PICK_ROM) {
            return;
        }

        mPickingRom = false;

        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null) {
            Log.w(TAG, "no ROM was picked");
            // There is no time limit on choosing a file, so cancelling the
            // picker does not end anything: offer to open it again.
            askForRomAgain();
            return;
        }

        try {
            copyToInternalStorage(data.getData(), mPendingRomName);
            onRomPickResult(true);
        } catch (Exception e) {
            Log.e(TAG, "could not copy the ROM: " + e);
            askForRomAgain();
        }
    }

    /**
     * The file picker was cancelled, or the file could not be copied.
     *
     * The player is asked whether they want to pick a file again. There is no
     * time limit and the app never gives up on its own; the only way out is
     * the exit button, or the system back button.
     */
    private void askForRomAgain() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (isFinishing()) {
                    return;
                }
                new AlertDialog.Builder(SDLActivity.this)
                        .setTitle("ROM required")
                        .setMessage("Perfect Dark needs its ROM file.\n\n"
                                + "Choose \"pd.ntsc-final.z64\" from your device. "
                                + "You can take as long as you like.")
                        .setPositiveButton("Choose file",
                                new DialogInterface.OnClickListener() {
                                    @Override
                                    public void onClick(DialogInterface d, int which) {
                                        pickRom(mPendingRomName);
                                    }
                                })
                        .setNegativeButton("Exit",
                                new DialogInterface.OnClickListener() {
                                    @Override
                                    public void onClick(DialogInterface d, int which) {
                                        mPickingRom = false;
                                        onRomPickResult(false);
                                    }
                                })
                        .setCancelable(false)
                        .show();
            }
        });
    }

    /**
     * Copies the picked file into the app's internal storage, where the port
     * looks for it.
     */
    private void copyToInternalStorage(Uri uri, String filename) throws Exception {
        if (filename == null || filename.length() == 0) {
            throw new Exception("no file name");
        }
        File dest = new File(getFilesDir(), filename);
        InputStream in = getContentResolver().openInputStream(uri);
        if (in == null) {
            throw new Exception("could not open " + uri);
        }
        try {
            OutputStream out = new FileOutputStream(dest);
            try {
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
                out.flush();
            } finally {
                out.close();
            }
        } finally {
            in.close();
        }
        Log.i(TAG, "copied the ROM to " + dest.getAbsolutePath()
                + " (" + dest.length() + " bytes)");
    }

    /** Called from native code once the picker has been answered. */
    private static void onRomPickResult(boolean success) {
        nativeOnRomPickResult(success);
    }

    private static native void nativeBackPressed();
    private static native void nativeSetActivity(Object activity);
    private static native void nativeOnRomPickResult(boolean success);
}
