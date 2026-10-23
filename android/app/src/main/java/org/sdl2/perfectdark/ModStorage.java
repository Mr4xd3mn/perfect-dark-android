package org.sdl2.perfectdark;

import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;

/** Imports a SAF tree into a private snapshot usable by the C filesystem. */
final class ModStorage {
    private static final String PREFS = "mods";
    private static final String ACTIVE = "active";
    private static final String LABEL = "label";
    private static final long MAX_BYTES = 2L * 1024 * 1024 * 1024;
    private static final int MAX_ENTRIES = 20000;
    private static final int MAX_DEPTH = 32;

    private ModStorage() {}

    static File activeDirectory(Context context) {
        return validatedDirectory(context, preferences(context).getString(ACTIVE, ""));
    }

    static File validatedDirectory(Context context, String name) {
        if (name == null || !name.matches("mod-[a-f0-9-]+")) return null;
        File dir = new File(root(context), name);
        return dir.isDirectory() ? dir : null;
    }

    static String activeLabel(Context context) {
        return preferences(context).getString(LABEL, "Imported mod");
    }

    static boolean disable(Context context) {
        return preferences(context).edit().remove(ACTIVE).remove(LABEL).commit();
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    private static File root(Context context) {
        return new File(context.getFilesDir(), "mods");
    }

    static void importTree(Context context, Uri tree) throws IOException {
        File parent = root(context);
        if (!parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Could not create mod storage.");
        }
        File staging = new File(parent, "mod-" + UUID.randomUUID());
        if (!staging.mkdir()) throw new IOException("Could not create mod folder.");
        boolean activated = false;
        try {
            String id = DocumentsContract.getTreeDocumentId(tree);
            Import operation = new Import(context, tree, staging);
            operation.copyDirectory(id, staging, 0);
            if (operation.files == 0) throw new IOException("The selected folder has no files.");
            if (!looksLikeMod(staging)) {
                throw new IOException("Choose the mod folder containing modconfig.txt, files, "
                        + "textures, animations, sequences or segs. Extract ZIP files first.");
            }
            String label = documentName(context, tree, id);
            // Publish only after every file has been copied. The old selection
            // remains usable if the provider fails or the device runs out of space.
            if (!preferences(context).edit().putString(ACTIVE, staging.getName())
                    .putString(LABEL, label).commit()) {
                throw new IOException("Could not save the mod selection.");
            }
            activated = true;
        } finally {
            if (!activated) deleteSnapshot(parent, staging);
        }
        // Old copies can be removed now; this runs before the game is started.
        cleanup(context);
    }

    private static boolean looksLikeMod(File directory) {
        if (new File(directory, "modconfig.txt").isFile()) return true;
        for (String name : new String[] {"files", "textures", "animations", "sequences", "segs"}) {
            if (new File(directory, name).isDirectory()) return true;
        }
        return false;
    }

    private static String documentName(Context context, Uri tree, String id) throws IOException {
        Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, id);
        try (Cursor cursor = context.getContentResolver().query(document,
                new String[] {DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) return cursor.getString(0);
        }
        return "Imported mod";
    }

    static void cleanup(Context context) {
        File parent = root(context);
        File active = activeDirectory(context);
        File[] snapshots = parent.listFiles();
        if (snapshots == null) return;
        for (File snapshot : snapshots) {
            if (!snapshot.equals(active) && snapshot.getName().startsWith("mod-")) {
                deleteSnapshot(parent, snapshot);
            }
        }
    }

    private static void deleteSnapshot(File parent, File file) {
        try {
            // Never follow a link or a path outside this app's private mod store.
            if (!file.getCanonicalPath().startsWith(parent.getCanonicalPath() + File.separator)) return;
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteSnapshot(parent, child);
            file.delete();
        } catch (IOException ignored) {
            // A failed cleanup must not invalidate the newly imported mod.
        }
    }

    private static final class Import {
        final Context context;
        final Uri tree;
        final File root;
        final Set<String> visited = new HashSet<>();
        long bytes;
        int entries;
        int files;

        Import(Context context, Uri tree, File root) {
            this.context = context;
            this.tree = tree;
            this.root = root;
        }

        void copyDirectory(String id, File destination, int depth) throws IOException {
            if (depth > MAX_DEPTH || !visited.add(id)) {
                throw new IOException("The mod folder is too deeply nested or contains a loop.");
            }
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
            String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE};
            try (Cursor cursor = context.getContentResolver().query(children, columns, null, null, null)) {
                if (cursor == null) throw new IOException("Could not read the selected folder.");
                while (cursor.moveToNext()) {
                    if (++entries > MAX_ENTRIES) throw new IOException("The mod contains too many files.");
                    String childId = cursor.getString(0);
                    String name = cursor.getString(1);
                    if (name == null || name.isEmpty() || name.equals(".") || name.equals("..")
                            || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0
                            || name.indexOf('\0') >= 0) {
                        throw new IOException("The mod contains an invalid file name.");
                    }
                    File target = new File(destination, name);
                    String path = target.getCanonicalPath();
                    if (!path.startsWith(root.getCanonicalPath() + File.separator)
                            || path.getBytes(java.nio.charset.StandardCharsets.UTF_8).length >= 1000) {
                        throw new IOException("The mod contains an invalid or overly long path.");
                    }
                    if (DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2))) {
                        if (!target.mkdir()) throw new IOException("Could not create folder: " + name);
                        copyDirectory(childId, target, depth + 1);
                    } else {
                        if (!target.createNewFile()) throw new IOException("Duplicate file name: " + name);
                        Uri source = DocumentsContract.buildDocumentUriUsingTree(tree, childId);
                        try (InputStream in = context.getContentResolver().openInputStream(source);
                             FileOutputStream out = new FileOutputStream(target)) {
                            if (in == null) throw new IOException("Could not read file: " + name);
                            byte[] buffer = new byte[65536];
                            int count;
                            while ((count = in.read(buffer)) != -1) {
                                bytes += count;
                                if (bytes > MAX_BYTES) throw new IOException("The mod exceeds the 2 GB import limit.");
                                out.write(buffer, 0, count);
                            }
                        }
                        files++;
                    }
                }
            }
        }
    }
}
