package android.database.sqlite;

public class SQLiteReadOnlyDatabaseException extends SQLiteException {
    public SQLiteReadOnlyDatabaseException() {}
    public SQLiteReadOnlyDatabaseException(String error) { super(error); }
    public SQLiteReadOnlyDatabaseException(String error, Throwable cause) { super(error, cause); }
}
