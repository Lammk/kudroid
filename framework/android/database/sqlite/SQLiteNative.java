package android.database.sqlite;

/** Static binding to the host SQLite; handles are native pointers carried as longs. */
final class SQLiteNative {
    private SQLiteNative() {}

    static native int isAvailable();
    static native long open(String path, int flags);
    static native String openError();
    static native void close(long db);
    static native String errorMessage(long db);
    static native int errorCode(long db);
    static native int exec(long db, String sql);
    static native int changes(long db);
    static native long lastInsertRowId(long db);

    static native long prepare(long db, String sql);
    static native void finalizeStmt(long stmt);
    static native void reset(long stmt);
    static native void clearBindings(long stmt);
    static native int bindParameterCount(long stmt);
    static native int bindNull(long stmt, int index);
    static native int bindLong(long stmt, int index, long value);
    static native int bindDouble(long stmt, int index, double value);
    static native int bindString(long stmt, int index, String value);
    static native int bindBlob(long stmt, int index, byte[] value);
    static native int step(long stmt);

    static native int columnCount(long stmt);
    static native String columnName(long stmt, int index);
    static native int columnType(long stmt, int index);
    static native long columnLong(long stmt, int index);
    static native double columnDouble(long stmt, int index);
    static native String columnString(long stmt, int index);
    static native byte[] columnBlob(long stmt, int index);

    static final int SQLITE_OK = 0;
    static final int SQLITE_BUSY = 5;
    static final int SQLITE_LOCKED = 6;
    static final int SQLITE_READONLY = 8;
    static final int SQLITE_IOERR = 10;
    static final int SQLITE_CORRUPT = 11;
    static final int SQLITE_FULL = 13;
    static final int SQLITE_CANTOPEN = 14;
    static final int SQLITE_CONSTRAINT = 19;
    static final int SQLITE_MISUSE = 21;
    static final int SQLITE_ROW = 100;
    static final int SQLITE_DONE = 101;

    static final int TYPE_INTEGER = 1;
    static final int TYPE_FLOAT = 2;
    static final int TYPE_TEXT = 3;
    static final int TYPE_BLOB = 4;
    static final int TYPE_NULL = 5;
}
