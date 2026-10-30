package android.database.sqlite;

public class SQLiteAccessPermException extends SQLiteException {
    public SQLiteAccessPermException() {}
    public SQLiteAccessPermException(String error) { super(error); }
    public SQLiteAccessPermException(String error, Throwable cause) { super(error, cause); }
}
