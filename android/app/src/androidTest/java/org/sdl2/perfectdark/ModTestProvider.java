package org.sdl2.perfectdark;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.content.ContentProvider;
import android.content.ContentValues;
import android.net.Uri;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;

/** Test-only SAF provider: nested files, bad names and interrupted reads. */
public class ModTestProvider extends ContentProvider {
    private static final String[] COLUMNS = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE};

    @Override public boolean onCreate() { return true; }
    @Override public Cursor query(Uri uri, String[] projection, String selection,
            String[] args, String sortOrder) {
        String id = DocumentsContract.getDocumentId(uri);
        return "children".equals(uri.getLastPathSegment())
                ? queryChildDocuments(id, projection, sortOrder) : queryDocument(id, projection);
    }
    @Override public String getType(Uri uri) { return "application/octet-stream"; }
    @Override public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException(); }
    @Override public int update(Uri uri, ContentValues values, String where, String[] args) { throw new UnsupportedOperationException(); }
    @Override public int delete(Uri uri, String where, String[] args) { throw new UnsupportedOperationException(); }
    @Override public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        return openDocument(DocumentsContract.getDocumentId(uri), mode, null);
    }

    private void row(MatrixCursor cursor, String id, String name, boolean directory) {
        Object[] values = new Object[cursor.getColumnCount()];
        String[] columns = cursor.getColumnNames();
        for (int i = 0; i < columns.length; i++) {
            if (columns[i].equals(COLUMNS[0])) values[i] = id;
            else if (columns[i].equals(COLUMNS[1])) values[i] = name;
            else if (columns[i].equals(COLUMNS[2])) values[i] = directory
                    ? DocumentsContract.Document.MIME_TYPE_DIR : "application/octet-stream";
        }
        cursor.addRow(values);
    }

    public Cursor queryDocument(String id, String[] projection) {
        MatrixCursor cursor = new MatrixCursor(projection == null ? COLUMNS : projection);
        row(cursor, id, "Test mod " + id, true);
        return cursor;
    }

    public Cursor queryChildDocuments(String id, String[] projection, String sortOrder) {
        MatrixCursor cursor = new MatrixCursor(projection == null ? COLUMNS : projection);
        if (id.equals("real") || id.startsWith("real/") || id.equals("nokia") || id.startsWith("nokia/")) {
            File[] children = realFile(id).listFiles();
            if (children != null) for (File child : children) {
                row(cursor, id + "/" + child.getName(), child.getName(), child.isDirectory());
            }
            return cursor;
        }
        switch (id) {
            case "valid": case "broken":
                row(cursor, id + "/config", "modconfig.txt", false);
                row(cursor, id + "/files", "files", true);
                break;
            case "valid/files": case "broken/files":
                row(cursor, id + "/asset", "sample.bin", false);
                break;
            case "traversal": row(cursor, "asset", "../escape", false); break;
            case "slash": row(cursor, "asset", "nested/file", false); break;
            case "backslash": row(cursor, "asset", "nested\\file", false); break;
            case "cycle": row(cursor, "cycle", "files", true); break;
            case "duplicate":
                row(cursor, "duplicate/asset", "modconfig.txt", false);
                row(cursor, "duplicate/asset", "modconfig.txt", false);
                break;
            case "wrong-root": row(cursor, "wrong-root/asset", "readme.txt", false); break;
            case "deep": row(cursor, "deep/1", "files", true); break;
            default:
                if (id.startsWith("deep/")) {
                    int level = Integer.parseInt(id.substring(5));
                    row(cursor, "deep/" + (level + 1), "dir", true);
                }
        }
        return cursor;
    }

    public ParcelFileDescriptor openDocument(String id, String mode,
            CancellationSignal signal) throws FileNotFoundException {
        if (id.startsWith("real/") || id.startsWith("nokia/")) {
            return ParcelFileDescriptor.open(realFile(id), ParcelFileDescriptor.MODE_READ_ONLY);
        }
        if (id.startsWith("broken/files/")) throw new FileNotFoundException("Simulated provider failure");
        File file = new File(getContext().getCacheDir(), "mod-fixture");
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write((id.endsWith("config") ? "# SAF import integration test\n" : "fixture bytes")
                    .getBytes(java.nio.charset.StandardCharsets.UTF_8));
        } catch (Exception e) {
            throw new FileNotFoundException(e.toString());
        }
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
    }

    private File realFile(String id) {
        if (id.equals("nokia") || id.startsWith("nokia/")) {
            File root = new File("/data/local/tmp/pd-nokia");
            return id.equals("nokia") ? root : new File(root, id.substring(6));
        }
        File root = new File("/data/local/tmp/pd-g5-carpark");
        return id.equals("real") ? root : new File(root, id.substring(5));
    }
}
