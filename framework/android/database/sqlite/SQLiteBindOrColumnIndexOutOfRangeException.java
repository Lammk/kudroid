package android.database.sqlite;

public class SQLiteBindOrColumnIndexOutOfRangeException extends SQLiteException {
    public SQLiteBindOrColumnIndexOutOfRangeException() {}
    public SQLiteBindOrColumnIndexOutOfRangeException(String error) { super(error); }
    public SQLiteBindOrColumnIndexOutOfRangeException(String error, Throwable cause) { super(error, cause); }
}
