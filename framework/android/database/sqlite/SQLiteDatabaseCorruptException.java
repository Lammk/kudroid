package android.database.sqlite;

public class SQLiteDatabaseCorruptException extends SQLiteException {
    public SQLiteDatabaseCorruptException() {}
    public SQLiteDatabaseCorruptException(String error) { super(error); }
    public SQLiteDatabaseCorruptException(String error, Throwable cause) { super(error, cause); }
}
