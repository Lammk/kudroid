package android.database.sqlite;

public class SQLiteDatabaseLockedException extends SQLiteException {
    public SQLiteDatabaseLockedException() {}
    public SQLiteDatabaseLockedException(String error) { super(error); }
    public SQLiteDatabaseLockedException(String error, Throwable cause) { super(error, cause); }
}
