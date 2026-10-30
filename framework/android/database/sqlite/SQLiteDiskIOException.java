package android.database.sqlite;

public class SQLiteDiskIOException extends SQLiteException {
    public SQLiteDiskIOException() {}
    public SQLiteDiskIOException(String error) { super(error); }
    public SQLiteDiskIOException(String error, Throwable cause) { super(error, cause); }
}
