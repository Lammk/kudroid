package android.database.sqlite;

public class SQLiteConstraintException extends SQLiteException {
    public SQLiteConstraintException() {}
    public SQLiteConstraintException(String error) { super(error); }
    public SQLiteConstraintException(String error, Throwable cause) { super(error, cause); }
}
