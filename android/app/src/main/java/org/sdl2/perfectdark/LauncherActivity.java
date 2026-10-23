package org.sdl2.perfectdark;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;

/** Select mods before SDL starts: native mods are loaded once at boot. */
public class LauncherActivity extends Activity {
    private static final int PICK_MOD = 0x5045;
    private TextView status;
    private Button play;
    private Button choose;
    private Button disable;
    private boolean importing;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        content.setPadding(pad, pad, pad, pad);
        scroll.addView(content);
        TextView title = new TextView(this);
        title.setText("Perfect Dark");
        title.setTextSize(28);
        content.addView(title);
        status = new TextView(this);
        status.setTextSize(17);
        status.setPadding(0, pad / 2, 0, pad / 2);
        content.addView(status);
        play = button(content, "Play", v -> {
            Intent game = new Intent(this, SDLActivity.class);
            File active = ModStorage.activeDirectory(this);
            if (active != null) game.putExtra("mod_directory", active.getName());
            play.setEnabled(false);
            startActivity(game);
        });
        choose = button(content, "Choose mod folder", v -> {
            try {
                Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                startActivityForResult(picker, PICK_MOD);
            } catch (Exception e) {
                showError("Could not open the folder picker.");
            }
        });
        disable = button(content, "Disable mod", v -> {
            if (!ModStorage.disable(this)) showError("Could not save the selection.");
            else {
                ModStorage.cleanup(this);
                refresh();
            }
        });
        TextView help = new TextView(this);
        help.setText("Optional: extract a PC-port mod, then choose the folder containing "
                + "modconfig.txt or its files/textures folders. A copy is stored in the app. "
                + "Choose it again to import changes. Mods requiring a different executable "
                + "or netplay build are not supported.");
        help.setTextSize(14);
        content.addView(help);
        setContentView(scroll);
        ModStorage.cleanup(this);
        refresh();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (status != null) refresh();
    }

    private Button button(LinearLayout parent, String label, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(label);
        button.setOnClickListener(listener);
        parent.addView(button);
        return button;
    }

    private void refresh() {
        boolean active = ModStorage.activeDirectory(this) != null;
        status.setText(importing ? "Importing mod… Keep the app open."
                : active ? "Mod: " + ModStorage.activeLabel(this) : "No mod selected — original game");
        play.setEnabled(!importing);
        choose.setEnabled(!importing);
        disable.setEnabled(!importing && active);
    }

    @Override
    public void onBackPressed() {
        if (!importing) super.onBackPressed();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_MOD || resultCode != RESULT_OK || data == null
                || data.getData() == null || importing) return;
        Uri tree = data.getData();
        importing = true;
        refresh();
        new Thread(() -> {
            String error = null;
            try {
                ModStorage.importTree(getApplicationContext(), tree);
            } catch (Exception e) {
                error = e.getMessage() == null ? "Could not read the selected folder." : e.getMessage();
            }
            final String message = error;
            runOnUiThread(() -> {
                importing = false;
                if (isFinishing() || isDestroyed()) return;
                refresh();
                if (message != null) showError(message);
            });
        }, "ModImport").start();
    }

    private void showError(String message) {
        new AlertDialog.Builder(this).setTitle("Mod not imported")
                .setMessage(message).setPositiveButton("OK", null).show();
    }
}
