package android.database.sqlite;

public class SQLiteException extends android.database.SQLException {
    public SQLiteException() {}
    public SQLiteException(String error) { super(error); }
    public SQLiteException(String error, Throwable cause) { super(error, cause); }
}
