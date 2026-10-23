package org.sdl2.perfectdark;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.FileInputStream;

/** No network or third-party test dependency; run with adb shell am instrument -w. */
public class ModTestRunner extends ModGameRunner {
    private int checks;
    private boolean gameSmoke;

    @Override public void onCreate(Bundle args) {
        gameSmoke = args != null && "true".equals(args.getString("smoke"));
        super.onCreate(args);
    }

    private void check(boolean condition, String description) {
        if (!condition) throw new AssertionError(description);
        checks++;
        Bundle progress = new Bundle();
        progress.putString("stream", "PASS: " + description + "\n");
        sendStatus(0, progress);
    }

    private void importFixture(Context context, String name) throws Exception {
        ModStorage.importTree(context, DocumentsContract.buildTreeDocumentUri(
                "org.sdl2.perfectdark.test.documents", name));
    }

    @Override public void onStart() {
        if (gameSmoke) {
            super.onStart();
            return;
        }
        Bundle result = new Bundle();
        try {
            Context context = getTargetContext();
            check(ModStorage.disable(context), "clear fixture selection");
            ModStorage.cleanup(context);
            importFixture(context, "valid");
            File original = ModStorage.activeDirectory(context);
            check(original != null, "successful import activates snapshot");
            check(new File(original, "modconfig.txt").isFile(), "root config copied");
            byte[] data = new byte[13];
            try (FileInputStream in = new FileInputStream(new File(original, "files/sample.bin"))) {
                check(in.read(data) == data.length && in.read() == -1, "copied file length preserved");
            }
            String content = new String(data, java.nio.charset.StandardCharsets.UTF_8);
            check(content.equals("fixture bytes"), "nested binary contents preserved");
            check(ModStorage.activeDirectory(context.createPackageContext(context.getPackageName(), 0))
                    .equals(original), "selection persists across contexts");
            check(ModStorage.validatedDirectory(context, "../") == null, "reject external mod path");
            for (String bad : new String[]{"empty", "traversal", "slash", "backslash", "cycle",
                    "duplicate", "wrong-root", "deep", "broken"}) {
                boolean rejected = false;
                try { importFixture(context, bad); } catch (Exception expected) { rejected = true; }
                check(rejected, "reject " + bad);
                check(original.equals(ModStorage.activeDirectory(context)), "preserve active mod after " + bad);
                check(new File(context.getFilesDir(), "mods").list().length == 1,
                        "remove partial import after " + bad);
            }
            importFixture(context, "valid");
            check(!original.exists(), "replacement removes old snapshot");
            check(ModStorage.disable(context), "disable saved mod");
            check(ModStorage.activeDirectory(context) == null, "disabled mod is not passed to game");
            ModStorage.cleanup(context);
            check(new File(context.getFilesDir(), "mods").list().length == 0, "remove disabled copy");
            Activity launcher = startActivitySync(new Intent(context, LauncherActivity.class)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            check(launcher != null, "launcher opens without native game or ROM");
            runOnMainSync(launcher::finish);
            result.putString("stream", "\nOK: " + checks + " checks passed\n");
            finish(Activity.RESULT_OK, result);
        } catch (Throwable error) {
            result.putString("stream", "\nFAILED: " + android.util.Log.getStackTraceString(error));
            finish(Activity.RESULT_CANCELED, result);
        }
    }
}
