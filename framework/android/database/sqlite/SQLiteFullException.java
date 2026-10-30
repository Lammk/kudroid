package android.database.sqlite;

public class SQLiteFullException extends SQLiteException {
    public SQLiteFullException() {}
    public SQLiteFullException(String error) { super(error); }
    public SQLiteFullException(String error, Throwable cause) { super(error, cause); }
}
