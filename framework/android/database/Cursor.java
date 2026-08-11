package android.database;

/**
 * Minimal android.database.Cursor implementation.
 *
 * Provides access to query results. For KuDroid's minimal framework, this is
 * an empty cursor (no rows).
 */
public interface Cursor {
    /** Return the number of rows. */
    int getCount();

    /** Move to the first row. */
    boolean moveToFirst();

    /** Move to the next row. */
    boolean moveToNext();

    /** Return whether the cursor is closed. */
    boolean isClosed();

    /** Close the cursor. */
    void close();

    /** Return a string value for the given column. */
    String getString(int columnIndex);

    /** Return an int value for the given column. */
    int getInt(int columnIndex);

    /** Return a long value for the given column. */
    long getLong(int columnIndex);

    /** Return a float value for the given column. */
    float getFloat(int columnIndex);

    /** Return a double value for the given column. */
    double getDouble(int columnIndex);

    /** Return whether the value at the given column is null. */
    boolean isNull(int columnIndex);
}
