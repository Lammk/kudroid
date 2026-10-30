package android.database.sqlite;

import android.content.Context;
import android.database.DatabaseErrorHandler;

import java.io.File;

/**
 * Manages the creation and versioning of a database file: onCreate for a new file,
 * onUpgrade / onDowngrade when the stored version differs, onOpen on every open.
 */
public abstract class SQLiteOpenHelper implements AutoCloseable {
    private final Context mContext;
    private final String mName;
    private final SQLiteDatabase.CursorFactory mFactory;
    private final int mNewVersion;
    private final DatabaseErrorHandler mErrorHandler;
    private SQLiteDatabase mDatabase;
    private boolean mIsInitializing;
    private boolean mEnableWriteAheadLogging;

    public SQLiteOpenHelper(Context context, String name, SQLiteDatabase.CursorFactory factory, int version) {
        this(context, name, factory, version, null);
    }

    public SQLiteOpenHelper(Context context, String name, SQLiteDatabase.CursorFactory factory,
            int version, DatabaseErrorHandler errorHandler) {
        if (version < 1) throw new IllegalArgumentException("Version must be >= 1, was " + version);
        mContext = context;
        mName = name;
        mFactory = factory;
        mNewVersion = version;
        mErrorHandler = errorHandler;
    }

    public SQLiteOpenHelper(Context context, String name, int version, SQLiteDatabase.OpenParams params) {
        this(context, name, null, version, null);
    }

    public String getDatabaseName() { return mName; }

    public void setWriteAheadLoggingEnabled(boolean enabled) {
        synchronized (this) {
            if (mEnableWriteAheadLogging != enabled) {
                if (mDatabase != null && mDatabase.isOpen() && !mDatabase.isReadOnly()) {
                    if (enabled) mDatabase.enableWriteAheadLogging();
                    else mDatabase.disableWriteAheadLogging();
                }
                mEnableWriteAheadLogging = enabled;
            }
        }
    }

    public void setIdleConnectionTimeout(long idleConnectionTimeoutMs) {}
    public void setLookasideConfig(int slotSize, int slotCount) {}
    public void setOpenParams(SQLiteDatabase.OpenParams openParams) {}

    public SQLiteDatabase getWritableDatabase() {
        synchronized (this) {
            return getDatabaseLocked(true);
        }
    }

    public SQLiteDatabase getReadableDatabase() {
        synchronized (this) {
            return getDatabaseLocked(false);
        }
    }

    private SQLiteDatabase getDatabaseLocked(boolean writable) {
        if (mDatabase != null) {
            if (!mDatabase.isOpen()) {
                mDatabase = null;
            } else if (!writable || !mDatabase.isReadOnly()) {
                return mDatabase;
            }
        }
        if (mIsInitializing) throw new IllegalStateException("getDatabase called recursively");

        SQLiteDatabase db = mDatabase;
        try {
            mIsInitializing = true;
            if (db == null) db = open(writable);
            onConfigure(db);

            final int version = db.getVersion();
            if (version != mNewVersion) {
                if (db.isReadOnly()) {
                    throw new SQLiteException("Can't upgrade read-only database from version "
                            + version + " to " + mNewVersion + ": " + mName);
                }
                db.beginTransaction();
                try {
                    if (version == 0) onCreate(db);
                    else if (version > mNewVersion) onDowngrade(db, version, mNewVersion);
                    else onUpgrade(db, version, mNewVersion);
                    db.setVersion(mNewVersion);
                    db.setTransactionSuccessful();
                } finally {
                    db.endTransaction();
                }
            }
            onOpen(db);
            mDatabase = db;
            return db;
        } finally {
            mIsInitializing = false;
            if (db != null && db != mDatabase) db.close();
        }
    }

    private SQLiteDatabase open(boolean writable) {
        if (mName == null) return SQLiteDatabase.create(null);
        final File file = mContext.getDatabasePath(mName);
        final File dir = file.getParentFile();
        if (dir != null && !dir.exists()) dir.mkdirs();
        int flags = SQLiteDatabase.CREATE_IF_NECESSARY;
        if (mEnableWriteAheadLogging) flags |= SQLiteDatabase.ENABLE_WRITE_AHEAD_LOGGING;
        try {
            return SQLiteDatabase.openDatabase(file.getPath(), mFactory, flags, mErrorHandler);
        } catch (SQLiteException e) {
            if (writable) throw e;
            return SQLiteDatabase.openDatabase(file.getPath(), mFactory, SQLiteDatabase.OPEN_READONLY,
                    mErrorHandler);
        }
    }

    @Override
    public synchronized void close() {
        if (mIsInitializing) throw new IllegalStateException("Closed during initialization");
        if (mDatabase != null && mDatabase.isOpen()) {
            mDatabase.close();
            mDatabase = null;
        }
    }

    public void onConfigure(SQLiteDatabase db) {}

    public abstract void onCreate(SQLiteDatabase db);

    public abstract void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion);

    public void onDowngrade(SQLiteDatabase db, int oldVersion, int newVersion) {
        throw new SQLiteException("Can't downgrade database from version " + oldVersion + " to " + newVersion);
    }

    public void onOpen(SQLiteDatabase db) {}
}
