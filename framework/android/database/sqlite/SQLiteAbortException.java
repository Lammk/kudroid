package android.database.sqlite;

public class SQLiteAbortException extends SQLiteException {
    public SQLiteAbortException() {}
    public SQLiteAbortException(String error) { super(error); }
    public SQLiteAbortException(String error, Throwable cause) { super(error, cause); }
}
