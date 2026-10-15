package android.database;

import java.io.Closeable;

public interface Cursor extends Closeable {
    int FIELD_TYPE_NULL = 0;
    int FIELD_TYPE_INTEGER = 1;
    int FIELD_TYPE_FLOAT = 2;
    int FIELD_TYPE_STRING = 3;
    int FIELD_TYPE_BLOB = 4;

    int getCount();
    int getPosition();
    boolean move(int offset);
    boolean moveToPosition(int position);
    boolean moveToFirst();
    boolean moveToLast();
    boolean moveToNext();
    boolean moveToPrevious();
    boolean isFirst();
    boolean isLast();
    boolean isBeforeFirst();
    boolean isAfterLast();
    int getColumnIndex(String columnName);
    int getColumnIndexOrThrow(String columnName) throws IllegalArgumentException;
    String getColumnName(int columnIndex);
    String[] getColumnNames();
    int getColumnCount();
    byte[] getBlob(int columnIndex);
    String getString(int columnIndex);
    short getShort(int columnIndex);
    int getInt(int columnIndex);
    long getLong(int columnIndex);
    float getFloat(int columnIndex);
    double getDouble(int columnIndex);
    int getType(int columnIndex);
    boolean isNull(int columnIndex);
    void close();
    boolean isClosed();
    void registerDataSetObserver(DataSetObserver observer);
    void unregisterDataSetObserver(DataSetObserver observer);
}
