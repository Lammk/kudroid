package android.database.sqlite;

/** A SELECT whose rows are read when a cursor asks for them. */
public final class SQLiteQuery extends SQLiteProgram {
    SQLiteQuery(SQLiteDatabase db, String query, Object[] bindArgs) {
        super(db, query, bindArgs);
    }

    /** Runs the query to completion and returns the column names and every row. */
    SQLiteCursor.Result runAll() {
        return database().runQuery(this);
    }

    @Override
    public String toString() {
        return "SQLiteQuery: " + sql();
    }
}
