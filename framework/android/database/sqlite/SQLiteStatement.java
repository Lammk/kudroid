package android.database.sqlite;

public final class SQLiteStatement extends SQLiteProgram {
    SQLiteStatement(SQLiteDatabase db, String sql, Object[] bindArgs) {
        super(db, sql, bindArgs);
    }

    public void execute() {
        database().runStatement(this, 0);
    }

    public int executeUpdateDelete() {
        return (int) database().runStatement(this, 1);
    }

    public long executeInsert() {
        return database().runStatement(this, 2);
    }

    public long simpleQueryForLong() {
        final Object value = database().runScalar(this);
        if (value == null) return 0L;
        if (value instanceof Number) return ((Number) value).longValue();
        try {
            return Long.parseLong(value.toString());
        } catch (NumberFormatException e) {
            return 0L;
        }
    }

    public String simpleQueryForString() {
        final Object value = database().runScalar(this);
        return value == null ? null : value.toString();
    }

    @Override
    public String toString() {
        return "SQLiteProgram: " + sql();
    }
}
