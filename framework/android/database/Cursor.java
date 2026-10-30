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

    default void copyStringToBuffer(int columnIndex, CharArrayBuffer buffer) {
        final String value = getString(columnIndex);
        if (value == null) {
            buffer.sizeCopied = 0;
            return;
        }
        buffer.data = value.toCharArray();
        buffer.sizeCopied = buffer.data.length;
    }

    default void deactivate() {}
    default boolean requery() { return false; }
    default void registerContentObserver(ContentObserver observer) {}
    default void unregisterContentObserver(ContentObserver observer) {}
    default void setNotificationUri(android.content.ContentResolver cr, android.net.Uri uri) {}
    default android.net.Uri getNotificationUri() { return null; }
    default boolean getWantsAllOnMoveCalls() { return false; }
    default void setExtras(android.os.Bundle extras) {}
    default android.os.Bundle getExtras() { return android.os.Bundle.EMPTY; }
    default android.os.Bundle respond(android.os.Bundle extras) { return android.os.Bundle.EMPTY; }
}
