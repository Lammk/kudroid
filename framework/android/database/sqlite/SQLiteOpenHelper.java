package android.database.sqlite;

import android.content.Context;

public abstract class SQLiteOpenHelper implements AutoCloseable {
    private final String mName;
    private final int mVersion;
    private SQLiteDatabase mDatabase;

    public SQLiteOpenHelper(Context context, String name, Object factory, int version) {
        mName = name;
        mVersion = version;
    }
    public String getDatabaseName() { return mName; }
    public synchronized SQLiteDatabase getWritableDatabase() {
        if (mDatabase == null) {
            mDatabase = new SQLiteDatabase();
            onCreate(mDatabase);
        }
        return mDatabase;
    }
    public synchronized SQLiteDatabase getReadableDatabase() {
        return getWritableDatabase();
    }
    public synchronized void close() {
        if (mDatabase != null) {
            mDatabase.close();
            mDatabase = null;
        }
    }
    public abstract void onCreate(SQLiteDatabase db);
    public abstract void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion);
    public void onOpen(SQLiteDatabase db) {}
}
