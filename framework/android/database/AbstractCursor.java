package android.database;

import java.util.ArrayList;

public abstract class AbstractCursor implements Cursor {
    protected int mPos = -1;
    protected boolean mClosed = false;
    private final ArrayList<DataSetObserver> mObservers = new ArrayList<DataSetObserver>();

    public abstract int getCount();
    public abstract String[] getColumnNames();
    public abstract String getString(int column);
    public abstract short getShort(int column);
    public abstract int getInt(int column);
    public abstract long getLong(int column);
    public abstract float getFloat(int column);
    public abstract double getDouble(int column);
    public abstract boolean isNull(int column);
    public byte[] getBlob(int column) { return new byte[0]; }
    public int getType(int column) { return FIELD_TYPE_STRING; }

    public int getColumnCount() { return getColumnNames().length; }
    public int getColumnIndex(String columnName) {
        String[] names = getColumnNames();
        for (int i = 0; i < names.length; i++) {
            if (names[i].equalsIgnoreCase(columnName)) return i;
        }
        return -1;
    }
    public int getColumnIndexOrThrow(String columnName) {
        int index = getColumnIndex(columnName);
        if (index < 0) throw new IllegalArgumentException("column '" + columnName + "' does not exist");
        return index;
    }
    public String getColumnName(int columnIndex) { return getColumnNames()[columnIndex]; }
    public final int getPosition() { return mPos; }
    public final boolean moveToPosition(int position) {
        int count = getCount();
        if (position >= count) { mPos = count; return false; }
        if (position < 0) { mPos = -1; return false; }
        mPos = position;
        return true;
    }
    public final boolean move(int offset) { return moveToPosition(mPos + offset); }
    public final boolean moveToFirst() { return moveToPosition(0); }
    public final boolean moveToLast() { return moveToPosition(getCount() - 1); }
    public final boolean moveToNext() { return moveToPosition(mPos + 1); }
    public final boolean moveToPrevious() { return moveToPosition(mPos - 1); }
    public final boolean isFirst() { return mPos == 0 && getCount() != 0; }
    public final boolean isLast() { int cnt = getCount(); return mPos == (cnt - 1) && cnt != 0; }
    public final boolean isBeforeFirst() { return getCount() == 0 || mPos == -1; }
    public final boolean isAfterLast() { int cnt = getCount(); return cnt == 0 || mPos == cnt; }
    public void close() { mClosed = true; }
    public boolean isClosed() { return mClosed; }
    public void registerDataSetObserver(DataSetObserver observer) { mObservers.add(observer); }
    public void unregisterDataSetObserver(DataSetObserver observer) { mObservers.remove(observer); }
}
