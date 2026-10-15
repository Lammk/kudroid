package android.database.sqlite;

import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import java.io.Closeable;

public class SQLiteDatabase implements Closeable {
    public static final int OPEN_READWRITE = 0;
    public static final int OPEN_READONLY = 1;
    public static final int CREATE_IF_NECESSARY = 0x10000000;

    public static SQLiteDatabase openOrCreateDatabase(String path, Object factory) {
        return new SQLiteDatabase();
    }
    public void execSQL(String sql) throws SQLiteException {}
    public void execSQL(String sql, Object[] bindArgs) throws SQLiteException {}
    public Cursor rawQuery(String sql, String[] selectionArgs) {
        return new MatrixCursor(new String[]{"_id"});
    }
    public long insert(String table, String nullColumnHack, ContentValues values) { return 1L; }
    public int update(String table, ContentValues values, String whereClause, String[] whereArgs) { return 1; }
    public int delete(String table, String whereClause, String[] whereArgs) { return 0; }
    public void beginTransaction() {}
    public void setTransactionSuccessful() {}
    public void endTransaction() {}
    public boolean inTransaction() { return false; }
    public boolean isOpen() { return true; }
    public void close() {}
}
