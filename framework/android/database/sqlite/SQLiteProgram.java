package android.database.sqlite;

import java.util.ArrayList;

/** A compiled-on-demand statement with remembered bindings (1-based, as in SQLite). */
public abstract class SQLiteProgram {
    private final SQLiteDatabase mDatabase;
    private final String mSql;
    private final ArrayList<Object> mBindArgs = new ArrayList<>();

    SQLiteProgram(SQLiteDatabase db, String sql, Object[] bindArgs) {
        mDatabase = db;
        mSql = sql;
        if (bindArgs != null) {
            for (int i = 0; i < bindArgs.length; i++) setArg(i + 1, bindArgs[i]);
        }
    }

    final SQLiteDatabase database() { return mDatabase; }
    final String sql() { return mSql; }

    public final int getUniqueId() { return -1; }

    private void setArg(int index, Object value) {
        if (index < 1) throw new IllegalArgumentException("Cannot bind argument at index " + index
                + " because the index is out of range.  The statement has no parameters.");
        while (mBindArgs.size() < index) mBindArgs.add(null);
        mBindArgs.set(index - 1, value);
    }

    public void bindNull(int index) { setArg(index, null); }
    public void bindLong(int index, long value) { setArg(index, value); }
    public void bindDouble(int index, double value) { setArg(index, value); }

    public void bindString(int index, String value) {
        if (value == null) throw new IllegalArgumentException("the bind value at index " + index + " is null");
        setArg(index, value);
    }

    public void bindBlob(int index, byte[] value) {
        if (value == null) throw new IllegalArgumentException("the bind value at index " + index + " is null");
        setArg(index, value);
    }

    public void bindAllArgsAsStrings(String[] bindArgs) {
        if (bindArgs == null) return;
        for (int i = bindArgs.length; i != 0; i--) bindString(i, bindArgs[i - 1]);
    }

    public void clearBindings() { mBindArgs.clear(); }

    public void close() {}

    /** Compiles the statement and applies the remembered bindings; the caller finalizes it. */
    final long prepareBound() {
        final long stmt = mDatabase.prepare(mSql);
        for (int i = 0; i < mBindArgs.size(); i++) {
            mDatabase.bindValue(stmt, i + 1, mBindArgs.get(i));
        }
        return stmt;
    }
}
