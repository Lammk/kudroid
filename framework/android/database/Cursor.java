package android.database;

/**
 * minimal android.database.cursor implementation.
 *
 * provides access to query results. for kudroid minimal framework, this is
 * an empty pointer (no rows).
 */
public interface Cursor {
    /** returns the number of rows. */
    int getCount();

    /** move to the first row. */
    boolean moveToFirst();

    /** move to the next row. */
    boolean moveToNext();

    /** returns whether the cursor is closed or not. */
    boolean isClosed();

    /** closes the cursor. */
    void close();

    /** returns a string value for the given column. */
    String getString(int columnIndex);

    /** returns an integer value for the given column. */
    int getInt(int columnIndex);

    /** returns a long value for the given column. */
    long getLong(int columnIndex);

    /** returns a float value for the given column. */
    float getFloat(int columnIndex);

    /** returns a double value for the given column. */
    double getDouble(int columnIndex);

    /** returns whether the value in the given column is empty or not. */
    boolean isNull(int columnIndex);
}
