package android.database.sqlite;

import android.database.AbstractCursor;
import android.database.Cursor;

import java.util.ArrayList;

/** A cursor over rows read once from a SQLiteQuery. */
public class SQLiteCursor extends AbstractCursor {
    /** Column names and rows of one query run. */
    static final class Result {
        final String[] columns;
        final ArrayList<Object[]> rows;

        Result(String[] columns, ArrayList<Object[]> rows) {
            this.columns = columns;
            this.rows = rows;
        }
    }

    private final SQLiteCursorDriver mDriver;
    private final String mEditTable;
    private final SQLiteQuery mQuery;
    private Result mResult;

    /** Reads the query's rows now; the query's bindings must already be set. */
    public SQLiteCursor(SQLiteCursorDriver driver, String editTable, SQLiteQuery query) {
        if (query == null) throw new IllegalArgumentException("query object cannot be null");
        mDriver = driver;
        mEditTable = editTable;
        mQuery = query;
        mResult = query.runAll();
    }

    public SQLiteDatabase getDatabase() { return mQuery.database(); }

    private Object value(int column) {
        if (mClosed) throw new android.database.StaleDataException("Attempted to access a cursor after it has been closed.");
        if (mPos < 0 || mPos >= mResult.rows.size()) {
            throw new android.database.CursorIndexOutOfBoundsException(mPos, mResult.rows.size());
        }
        if (column < 0 || column >= mResult.columns.length) {
            throw new android.database.CursorIndexOutOfBoundsException("Requested column: " + column
                    + ", # of columns: " + mResult.columns.length);
        }
        return mResult.rows.get(mPos)[column];
    }

    @Override
    public int getCount() { return mResult.rows.size(); }

    @Override
    public String[] getColumnNames() { return mResult.columns; }

    @Override
    public String getString(int column) {
        final Object v = value(column);
        if (v == null) return null;
        if (v instanceof byte[]) throw new SQLiteException("Unable to convert BLOB to string");
        return v.toString();
    }

    @Override
    public short getShort(int column) { return (short) getLong(column); }

    @Override
    public int getInt(int column) { return (int) getLong(column); }

    @Override
    public long getLong(int column) {
        final Object v = value(column);
        if (v == null) return 0L;
        if (v instanceof Number) return ((Number) v).longValue();
        if (v instanceof byte[]) throw new SQLiteException("Unable to convert BLOB to long");
        try {
            return Long.parseLong(v.toString().trim());
        } catch (NumberFormatException e) {
            try {
                return (long) Double.parseDouble(v.toString().trim());
            } catch (NumberFormatException e2) {
                return 0L;
            }
        }
    }

    @Override
    public float getFloat(int column) { return (float) getDouble(column); }

    @Override
    public double getDouble(int column) {
        final Object v = value(column);
        if (v == null) return 0.0;
        if (v instanceof Number) return ((Number) v).doubleValue();
        if (v instanceof byte[]) throw new SQLiteException("Unable to convert BLOB to double");
        try {
            return Double.parseDouble(v.toString().trim());
        } catch (NumberFormatException e) {
            return 0.0;
        }
    }

    @Override
    public byte[] getBlob(int column) {
        final Object v = value(column);
        if (v == null) return null;
        if (v instanceof byte[]) return (byte[]) v;
        throw new SQLiteException("Unable to convert " + v.getClass().getSimpleName() + " to blob");
    }

    @Override
    public boolean isNull(int column) { return value(column) == null; }

    @Override
    public int getType(int column) {
        final Object v = value(column);
        if (v == null) return Cursor.FIELD_TYPE_NULL;
        if (v instanceof Long) return Cursor.FIELD_TYPE_INTEGER;
        if (v instanceof Double) return Cursor.FIELD_TYPE_FLOAT;
        if (v instanceof byte[]) return Cursor.FIELD_TYPE_BLOB;
        return Cursor.FIELD_TYPE_STRING;
    }

    @Override
    public void close() {
        super.close();
        if (mDriver != null) mDriver.cursorClosed();
    }

    public boolean requery() {
        if (mClosed) return false;
        mResult = mQuery.runAll();
        mPos = -1;
        return true;
    }
}
