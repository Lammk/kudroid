package android.database.sqlite;

public class SQLiteMisuseException extends SQLiteException {
    public SQLiteMisuseException() {}
    public SQLiteMisuseException(String error) { super(error); }
    public SQLiteMisuseException(String error, Throwable cause) { super(error, cause); }
}
