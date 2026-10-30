package android.database.sqlite;

public class SQLiteTableLockedException extends SQLiteException {
    public SQLiteTableLockedException() {}
    public SQLiteTableLockedException(String error) { super(error); }
    public SQLiteTableLockedException(String error, Throwable cause) { super(error, cause); }
}
