package android.database.sqlite;

import android.content.ContentValues;
import android.database.Cursor;
import android.database.DatabaseErrorHandler;
import android.database.SQLException;
import android.os.CancellationSignal;

import java.io.Closeable;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.locks.ReentrantLock;

/**
 * A SQLite database on the host's SQLite. One connection is shared by every thread: each
 * operation takes the connection lock, and a transaction keeps it until it ends, so other
 * threads wait for the writer as they do on Android.
 */
public class SQLiteDatabase implements Closeable {
    public static final int OPEN_READWRITE = 0x00000000;
    public static final int OPEN_READONLY = 0x00000001;
    public static final int NO_LOCALIZED_COLLATORS = 0x00000010;
    public static final int CREATE_IF_NECESSARY = 0x10000000;
    public static final int ENABLE_WRITE_AHEAD_LOGGING = 0x20000000;
    public static final int SQLITE_MAX_LIKE_PATTERN_LENGTH = 50000;
    public static final int CONFLICT_ROLLBACK = 1;
    public static final int CONFLICT_ABORT = 2;
    public static final int CONFLICT_FAIL = 3;
    public static final int CONFLICT_IGNORE = 4;
    public static final int CONFLICT_REPLACE = 5;
    public static final int CONFLICT_NONE = 0;

    private static final String[] CONFLICT_VALUES = {"", " OR ROLLBACK ", " OR ABORT ", " OR FAIL ",
            " OR IGNORE ", " OR REPLACE "};

    // The flags SQLite itself understands.
    private static final int SQLITE_OPEN_READONLY = 0x00000001;
    private static final int SQLITE_OPEN_READWRITE = 0x00000002;
    private static final int SQLITE_OPEN_CREATE = 0x00000004;

    public interface CursorFactory {
        Cursor newCursor(SQLiteDatabase db, SQLiteCursorDriver masterQuery, String editTable,
                SQLiteQuery query);
    }

    public interface OpenParams {}

    private final String mPath;
    private final int mFlags;
    private final CursorFactory mFactory;
    private final DatabaseErrorHandler mErrorHandler;
    private final ReentrantLock mLock = new ReentrantLock();
    private long mHandle;
    private boolean mWalEnabled;
    private final ArrayList<Boolean> mTransactions = new ArrayList<>();
    private final ArrayList<SQLiteTransactionListener> mListeners = new ArrayList<>();
    private boolean mChildFailed;
    private Object mScalar;

    private SQLiteDatabase(String path, int flags, CursorFactory factory, DatabaseErrorHandler handler) {
        mPath = path;
        mFlags = flags;
        mFactory = factory;
        mErrorHandler = handler;
    }

    // ---- opening and closing

    public static SQLiteDatabase openDatabase(String path, CursorFactory factory, int flags) {
        return openDatabase(path, factory, flags, null);
    }

    public static SQLiteDatabase openDatabase(String path, CursorFactory factory, int flags,
            DatabaseErrorHandler errorHandler) {
        final SQLiteDatabase db = new SQLiteDatabase(path, flags, factory, errorHandler);
        db.open();
        return db;
    }

    public static SQLiteDatabase openOrCreateDatabase(File file, CursorFactory factory) {
        return openOrCreateDatabase(file.getPath(), factory);
    }

    public static SQLiteDatabase openOrCreateDatabase(String path, CursorFactory factory) {
        return openDatabase(path, factory, CREATE_IF_NECESSARY, null);
    }

    public static SQLiteDatabase openOrCreateDatabase(String path, CursorFactory factory,
            DatabaseErrorHandler errorHandler) {
        return openDatabase(path, factory, CREATE_IF_NECESSARY, errorHandler);
    }

    public static SQLiteDatabase create(CursorFactory factory) {
        return openDatabase(":memory:", factory, CREATE_IF_NECESSARY);
    }

    public static boolean deleteDatabase(File file) {
        if (file == null) throw new IllegalArgumentException("file must not be null");
        boolean deleted = file.delete();
        deleted |= new File(file.getPath() + "-journal").delete();
        deleted |= new File(file.getPath() + "-wal").delete();
        deleted |= new File(file.getPath() + "-shm").delete();
        return deleted;
    }

    private void open() {
        if (SQLiteNative.isAvailable() == 0) {
            throw new SQLiteCantOpenDatabaseException("SQLite is not available in this build");
        }
        int native_ = (mFlags & OPEN_READONLY) != 0 ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
        if ((mFlags & CREATE_IF_NECESSARY) != 0 && (mFlags & OPEN_READONLY) == 0) {
            native_ |= SQLITE_OPEN_CREATE;
        }
        mHandle = SQLiteNative.open(mPath, native_);
        if (mHandle == 0) {
            throw new SQLiteCantOpenDatabaseException("Cannot open database '" + mPath + "': "
                    + SQLiteNative.openError());
        }
        if ((mFlags & ENABLE_WRITE_AHEAD_LOGGING) != 0) enableWriteAheadLogging();
    }

    public boolean isOpen() { return mHandle != 0; }
    public boolean isReadOnly() { return (mFlags & OPEN_READONLY) != 0; }
    public final String getPath() { return mPath; }
    public boolean isInMemoryDatabase() { return ":memory:".equals(mPath); }

    @Override
    public void close() {
        mLock.lock();
        try {
            if (mHandle != 0) {
                SQLiteNative.close(mHandle);
                mHandle = 0;
            }
        } finally {
            mLock.unlock();
        }
    }

    public void releaseReference() { close(); }
    public void acquireReference() {}

    private void checkOpen() {
        if (mHandle == 0) throw new IllegalStateException("attempt to re-open an already-closed object: " + this);
    }

    @Override
    public String toString() { return "SQLiteDatabase: " + mPath; }

    // ---- errors

    private SQLiteException failure(String what, String sql) {
        final int code = SQLiteNative.errorCode(mHandle);
        final String message = SQLiteNative.errorMessage(mHandle) + " (code " + code + ")"
                + (what != null ? " " + what : "") + (sql != null ? ": " + sql : "");
        switch (code & 0xff) {
            case SQLiteNative.SQLITE_CONSTRAINT: return new SQLiteConstraintException(message);
            case SQLiteNative.SQLITE_BUSY:
            case SQLiteNative.SQLITE_LOCKED: return new SQLiteDatabaseLockedException(message);
            case SQLiteNative.SQLITE_READONLY: return new SQLiteReadOnlyDatabaseException(message);
            case SQLiteNative.SQLITE_IOERR: return new SQLiteDiskIOException(message);
            case SQLiteNative.SQLITE_CORRUPT:
                if (mErrorHandler != null) mErrorHandler.onCorruption(this);
                return new SQLiteDatabaseCorruptException(message);
            case SQLiteNative.SQLITE_FULL: return new SQLiteFullException(message);
            case SQLiteNative.SQLITE_CANTOPEN: return new SQLiteCantOpenDatabaseException(message);
            case SQLiteNative.SQLITE_MISUSE: return new SQLiteMisuseException(message);
            default: return new SQLiteException(message);
        }
    }

    // ---- statements shared by the program classes

    long prepare(String sql) {
        checkOpen();
        final long stmt = SQLiteNative.prepare(mHandle, sql);
        if (stmt == 0) throw failure("while compiling", sql);
        return stmt;
    }

    void bindValue(long stmt, int index, Object value) {
        final int rc;
        if (value == null) rc = SQLiteNative.bindNull(stmt, index);
        else if (value instanceof Boolean) rc = SQLiteNative.bindLong(stmt, index, ((Boolean) value) ? 1L : 0L);
        else if (value instanceof Long || value instanceof Integer || value instanceof Short
                || value instanceof Byte) rc = SQLiteNative.bindLong(stmt, index, ((Number) value).longValue());
        else if (value instanceof Float || value instanceof Double) {
            rc = SQLiteNative.bindDouble(stmt, index, ((Number) value).doubleValue());
        } else if (value instanceof byte[]) rc = SQLiteNative.bindBlob(stmt, index, (byte[]) value);
        else rc = SQLiteNative.bindString(stmt, index, value.toString());
        if (rc != SQLiteNative.SQLITE_OK) {
            SQLiteNative.finalizeStmt(stmt);
            throw new SQLiteBindOrColumnIndexOutOfRangeException("Cannot bind argument at index "
                    + index + " because the index is out of range.");
        }
    }

    /** Runs a non-query statement; kind 0 = nothing, 1 = rows changed, 2 = inserted row id. */
    long runStatement(SQLiteProgram program, int kind) {
        mLock.lock();
        try {
            final long stmt = program.prepareBound();
            try {
                int rc = SQLiteNative.step(stmt);
                while (rc == SQLiteNative.SQLITE_ROW) rc = SQLiteNative.step(stmt);
                if (rc != SQLiteNative.SQLITE_DONE) throw failure("while executing", program.sql());
                if (kind == 1) return SQLiteNative.changes(mHandle);
                if (kind == 2) {
                    return SQLiteNative.changes(mHandle) > 0 ? SQLiteNative.lastInsertRowId(mHandle) : -1L;
                }
                return 0L;
            } finally {
                SQLiteNative.finalizeStmt(stmt);
            }
        } finally {
            mLock.unlock();
        }
    }

    /** The first column of the first row. */
    Object runScalar(SQLiteProgram program) {
        mLock.lock();
        try {
            final long stmt = program.prepareBound();
            try {
                final int rc = SQLiteNative.step(stmt);
                if (rc == SQLiteNative.SQLITE_DONE) {
                    throw new SQLiteDoneException("Failed to get a value from the first row");
                }
                if (rc != SQLiteNative.SQLITE_ROW) throw failure("while executing", program.sql());
                return columnValue(stmt, 0);
            } finally {
                SQLiteNative.finalizeStmt(stmt);
            }
        } finally {
            mLock.unlock();
        }
    }

    private static Object columnValue(long stmt, int column) {
        switch (SQLiteNative.columnType(stmt, column)) {
            case SQLiteNative.TYPE_INTEGER: return SQLiteNative.columnLong(stmt, column);
            case SQLiteNative.TYPE_FLOAT: return SQLiteNative.columnDouble(stmt, column);
            case SQLiteNative.TYPE_TEXT: return SQLiteNative.columnString(stmt, column);
            case SQLiteNative.TYPE_BLOB: return SQLiteNative.columnBlob(stmt, column);
            default: return null;
        }
    }

    SQLiteCursor.Result runQuery(SQLiteQuery query) {
        mLock.lock();
        try {
            final long stmt = query.prepareBound();
            try {
                final int count = SQLiteNative.columnCount(stmt);
                final String[] columns = new String[count];
                for (int i = 0; i < count; i++) columns[i] = SQLiteNative.columnName(stmt, i);
                final ArrayList<Object[]> rows = new ArrayList<>();
                int rc;
                while ((rc = SQLiteNative.step(stmt)) == SQLiteNative.SQLITE_ROW) {
                    final Object[] row = new Object[count];
                    for (int i = 0; i < count; i++) row[i] = columnValue(stmt, i);
                    rows.add(row);
                }
                if (rc != SQLiteNative.SQLITE_DONE) throw failure("while executing", query.sql());
                return new SQLiteCursor.Result(columns, rows);
            } finally {
                SQLiteNative.finalizeStmt(stmt);
            }
        } finally {
            mLock.unlock();
        }
    }

    public SQLiteStatement compileStatement(String sql) throws SQLException {
        checkOpen();
        // Compile once now so syntax errors surface here, as they do on Android.
        final long stmt = prepare(sql);
        SQLiteNative.finalizeStmt(stmt);
        return new SQLiteStatement(this, sql, null);
    }

    // ---- executing SQL

    public void execSQL(String sql) throws SQLException {
        execSQL(sql, null);
    }

    public void execSQL(String sql, Object[] bindArgs) throws SQLException {
        if (sql == null) throw new IllegalArgumentException("sql must not be null");
        checkOpen();
        if (bindArgs == null || bindArgs.length == 0) {
            mLock.lock();
            try {
                if (SQLiteNative.exec(mHandle, sql) != SQLiteNative.SQLITE_OK) throw failure("while executing", sql);
            } finally {
                mLock.unlock();
            }
            return;
        }
        new SQLiteStatement(this, sql, bindArgs).execute();
    }

    public void execPerConnectionSQL(String sql, Object[] bindArgs) throws SQLException {
        execSQL(sql, bindArgs);
    }

    // ---- queries

    public Cursor rawQuery(String sql, String[] selectionArgs) {
        return rawQueryWithFactory(null, sql, selectionArgs, null);
    }

    public Cursor rawQuery(String sql, String[] selectionArgs, CancellationSignal cancellationSignal) {
        return rawQueryWithFactory(null, sql, selectionArgs, null);
    }

    public Cursor rawQueryWithFactory(CursorFactory cursorFactory, String sql, String[] selectionArgs,
            String editTable) {
        return rawQueryWithFactory(cursorFactory, sql, selectionArgs, editTable, null);
    }

    public Cursor rawQueryWithFactory(CursorFactory cursorFactory, String sql, String[] selectionArgs,
            String editTable, CancellationSignal cancellationSignal) {
        checkOpen();
        final CursorFactory factory = cursorFactory != null ? cursorFactory : mFactory;
        final SQLiteDatabase self = this;
        final SQLiteCursorDriver driver = new SQLiteCursorDriver() {
            private String[] mArgs = selectionArgs;

            @Override
            public Cursor query(CursorFactory f, String[] bindArgs) {
                final SQLiteQuery query = new SQLiteQuery(self, sql, null);
                query.bindAllArgsAsStrings(bindArgs != null ? bindArgs : mArgs);
                if (f == null) return new SQLiteCursor(this, editTable, query);
                return f.newCursor(self, this, editTable, query);
            }

            @Override
            public void cursorDeactivated() {}

            @Override
            public void cursorRequeried(Cursor cursor) {}

            @Override
            public void cursorClosed() {}

            @Override
            public void setBindArguments(String[] bindArgs) { mArgs = bindArgs; }
        };
        return driver.query(factory, selectionArgs);
    }

    public Cursor query(String table, String[] columns, String selection, String[] selectionArgs,
            String groupBy, String having, String orderBy) {
        return query(false, table, columns, selection, selectionArgs, groupBy, having, orderBy, null);
    }

    public Cursor query(String table, String[] columns, String selection, String[] selectionArgs,
            String groupBy, String having, String orderBy, String limit) {
        return query(false, table, columns, selection, selectionArgs, groupBy, having, orderBy, limit);
    }

    public Cursor query(boolean distinct, String table, String[] columns, String selection,
            String[] selectionArgs, String groupBy, String having, String orderBy, String limit) {
        return queryWithFactory(null, distinct, table, columns, selection, selectionArgs, groupBy,
                having, orderBy, limit);
    }

    public Cursor query(boolean distinct, String table, String[] columns, String selection,
            String[] selectionArgs, String groupBy, String having, String orderBy, String limit,
            CancellationSignal cancellationSignal) {
        return query(distinct, table, columns, selection, selectionArgs, groupBy, having, orderBy, limit);
    }

    public Cursor queryWithFactory(CursorFactory cursorFactory, boolean distinct, String table,
            String[] columns, String selection, String[] selectionArgs, String groupBy, String having,
            String orderBy, String limit) {
        final String sql = buildQueryString(distinct, table, columns, selection, groupBy, having,
                orderBy, limit);
        return rawQueryWithFactory(cursorFactory, sql, selectionArgs, table);
    }

    public Cursor queryWithFactory(CursorFactory cursorFactory, boolean distinct, String table,
            String[] columns, String selection, String[] selectionArgs, String groupBy, String having,
            String orderBy, String limit, CancellationSignal cancellationSignal) {
        return queryWithFactory(cursorFactory, distinct, table, columns, selection, selectionArgs,
                groupBy, having, orderBy, limit);
    }

    private static String buildQueryString(boolean distinct, String table, String[] columns,
            String where, String groupBy, String having, String orderBy, String limit) {
        if (isEmpty(groupBy) && !isEmpty(having)) {
            throw new IllegalArgumentException("HAVING clauses are only permitted when using a groupBy clause");
        }
        final StringBuilder query = new StringBuilder(120);
        query.append("SELECT ");
        if (distinct) query.append("DISTINCT ");
        if (columns != null && columns.length != 0) {
            for (int i = 0; i < columns.length; i++) {
                if (i > 0) query.append(", ");
                query.append(columns[i]);
            }
            query.append(' ');
        } else {
            query.append("* ");
        }
        query.append("FROM ").append(table);
        appendClause(query, " WHERE ", where);
        appendClause(query, " GROUP BY ", groupBy);
        appendClause(query, " HAVING ", having);
        appendClause(query, " ORDER BY ", orderBy);
        appendClause(query, " LIMIT ", limit);
        return query.toString();
    }

    private static boolean isEmpty(String s) { return s == null || s.isEmpty(); }

    private static void appendClause(StringBuilder s, String name, String clause) {
        if (!isEmpty(clause)) s.append(name).append(clause);
    }

    // ---- insert / update / delete

    public long insert(String table, String nullColumnHack, ContentValues values) {
        try {
            return insertWithOnConflict(table, nullColumnHack, values, CONFLICT_NONE);
        } catch (SQLException e) {
            android.util.Log.e("SQLiteDatabase", "Error inserting into " + table, e);
            return -1;
        }
    }

    public long insertOrThrow(String table, String nullColumnHack, ContentValues values)
            throws SQLException {
        return insertWithOnConflict(table, nullColumnHack, values, CONFLICT_NONE);
    }

    public long replace(String table, String nullColumnHack, ContentValues initialValues) {
        try {
            return insertWithOnConflict(table, nullColumnHack, initialValues, CONFLICT_REPLACE);
        } catch (SQLException e) {
            android.util.Log.e("SQLiteDatabase", "Error inserting into " + table, e);
            return -1;
        }
    }

    public long replaceOrThrow(String table, String nullColumnHack, ContentValues initialValues)
            throws SQLException {
        return insertWithOnConflict(table, nullColumnHack, initialValues, CONFLICT_REPLACE);
    }

    public long insertWithOnConflict(String table, String nullColumnHack,
            ContentValues initialValues, int conflictAlgorithm) {
        final StringBuilder sql = new StringBuilder();
        sql.append("INSERT").append(CONFLICT_VALUES[conflictAlgorithm]).append(" INTO ").append(table).append('(');
        Object[] bindArgs = null;
        final int size = initialValues != null && !initialValues.isEmpty() ? initialValues.size() : 0;
        if (size > 0) {
            bindArgs = new Object[size];
            int i = 0;
            for (Map.Entry<String, Object> entry : initialValues.valueSet()) {
                sql.append(i > 0 ? "," : "").append(entry.getKey());
                bindArgs[i++] = entry.getValue();
            }
            sql.append(')').append(" VALUES (");
            for (i = 0; i < size; i++) sql.append(i > 0 ? ",?" : "?");
        } else {
            sql.append(nullColumnHack).append(") VALUES (NULL");
        }
        sql.append(')');
        return new SQLiteStatement(this, sql.toString(), bindArgs).executeInsert();
    }

    public int update(String table, ContentValues values, String whereClause, String[] whereArgs) {
        return updateWithOnConflict(table, values, whereClause, whereArgs, CONFLICT_NONE);
    }

    public int updateWithOnConflict(String table, ContentValues values, String whereClause,
            String[] whereArgs, int conflictAlgorithm) {
        if (values == null || values.isEmpty()) throw new IllegalArgumentException("Empty values");
        final StringBuilder sql = new StringBuilder(120);
        sql.append("UPDATE ").append(CONFLICT_VALUES[conflictAlgorithm]).append(table).append(" SET ");
        final int setValuesSize = values.size();
        final int bindArgsSize = whereArgs == null ? setValuesSize : setValuesSize + whereArgs.length;
        final Object[] bindArgs = new Object[bindArgsSize];
        int i = 0;
        for (Map.Entry<String, Object> entry : values.valueSet()) {
            sql.append(i > 0 ? "," : "").append(entry.getKey());
            bindArgs[i++] = entry.getValue();
            sql.append("=?");
        }
        if (whereArgs != null) {
            for (i = setValuesSize; i < bindArgsSize; i++) bindArgs[i] = whereArgs[i - setValuesSize];
        }
        if (!isEmpty(whereClause)) sql.append(" WHERE ").append(whereClause);
        return new SQLiteStatement(this, sql.toString(), bindArgs).executeUpdateDelete();
    }

    public int delete(String table, String whereClause, String[] whereArgs) {
        return new SQLiteStatement(this, "DELETE FROM " + table
                + (!isEmpty(whereClause) ? " WHERE " + whereClause : ""), (Object[]) whereArgs).executeUpdateDelete();
    }

    // ---- transactions

    public void beginTransaction() { beginTransaction(null, true); }
    public void beginTransactionNonExclusive() { beginTransaction(null, false); }
    public void beginTransactionWithListener(SQLiteTransactionListener l) { beginTransaction(l, true); }
    public void beginTransactionWithListenerNonExclusive(SQLiteTransactionListener l) { beginTransaction(l, false); }

    private void beginTransaction(SQLiteTransactionListener listener, boolean exclusive) {
        checkOpen();
        mLock.lock();
        if (mTransactions.isEmpty()) {
            if (SQLiteNative.exec(mHandle, exclusive ? "BEGIN EXCLUSIVE" : "BEGIN IMMEDIATE") != SQLiteNative.SQLITE_OK) {
                final SQLiteException e = failure("while beginning a transaction", null);
                mLock.unlock();
                throw e;
            }
            mChildFailed = false;
        }
        mTransactions.add(Boolean.FALSE);
        mListeners.add(listener);
        if (listener != null) listener.onBegin();
    }

    public void setTransactionSuccessful() {
        if (mTransactions.isEmpty() || !mLock.isHeldByCurrentThread()) {
            throw new IllegalStateException("no transaction pending");
        }
        if (mTransactions.get(mTransactions.size() - 1)) {
            throw new IllegalStateException("setTransactionSuccessful may only be called once per call to beginTransaction");
        }
        mTransactions.set(mTransactions.size() - 1, Boolean.TRUE);
    }

    public void endTransaction() {
        if (mTransactions.isEmpty() || !mLock.isHeldByCurrentThread()) {
            throw new IllegalStateException("no transaction pending");
        }
        try {
            final int top = mTransactions.size() - 1;
            final boolean successful = mTransactions.remove(top);
            final SQLiteTransactionListener listener = mListeners.remove(top);
            if (!successful) mChildFailed = true;
            if (mTransactions.isEmpty()) {
                final boolean commit = successful && !mChildFailed;
                try {
                    if (commit) {
                        if (listener != null) listener.onCommit();
                        if (SQLiteNative.exec(mHandle, "COMMIT") != SQLiteNative.SQLITE_OK) {
                            SQLiteNative.exec(mHandle, "ROLLBACK");
                            throw failure("while committing", null);
                        }
                    } else {
                        if (listener != null) listener.onRollback();
                        SQLiteNative.exec(mHandle, "ROLLBACK");
                    }
                } finally {
                    mChildFailed = false;
                }
            } else if (listener != null && !successful) {
                listener.onRollback();
            }
        } finally {
            mLock.unlock();
        }
    }

    public boolean inTransaction() { return !mTransactions.isEmpty(); }
    public boolean isDbLockedByCurrentThread() { return mLock.isHeldByCurrentThread(); }
    public boolean isDbLockedByOtherThreads() { return false; }
    public boolean yieldIfContended() { return false; }
    public boolean yieldIfContendedSafely() { return false; }
    public boolean yieldIfContendedSafely(long sleepAfterYieldDelay) { return false; }

    // ---- configuration

    public int getVersion() {
        return (int) longPragma("PRAGMA user_version", 0);
    }

    public void setVersion(int version) {
        execSQL("PRAGMA user_version = " + version);
    }

    public long getPageSize() { return longPragma("PRAGMA page_size", 4096); }

    public void setPageSize(long numBytes) { execSQL("PRAGMA page_size = " + numBytes); }

    public long getMaximumSize() { return longPragma("PRAGMA max_page_count", 0) * getPageSize(); }

    public long setMaximumSize(long numBytes) {
        final long pageSize = getPageSize();
        long numPages = numBytes / pageSize;
        if (numBytes % pageSize != 0) numPages++;
        return longPragma("PRAGMA max_page_count = " + numPages, 0) * pageSize;
    }

    private long longPragma(String sql, long fallback) {
        checkOpen();
        mLock.lock();
        try {
            final long stmt = prepare(sql);
            try {
                return SQLiteNative.step(stmt) == SQLiteNative.SQLITE_ROW
                        ? SQLiteNative.columnLong(stmt, 0) : fallback;
            } finally {
                SQLiteNative.finalizeStmt(stmt);
            }
        } finally {
            mLock.unlock();
        }
    }

    public void setForeignKeyConstraintsEnabled(boolean enable) {
        execSQL("PRAGMA foreign_keys = " + (enable ? "ON" : "OFF"));
    }

    public boolean enableWriteAheadLogging() {
        if (isReadOnly() || isInMemoryDatabase()) return false;
        try {
            execSQL("PRAGMA journal_mode = WAL");
            mWalEnabled = true;
        } catch (SQLiteException e) {
            mWalEnabled = false;
        }
        return mWalEnabled;
    }

    public void disableWriteAheadLogging() {
        if (!mWalEnabled) return;
        try {
            execSQL("PRAGMA journal_mode = DELETE");
        } catch (SQLiteException ignored) {}
        mWalEnabled = false;
    }

    public boolean isWriteAheadLoggingEnabled() { return mWalEnabled; }

    public void setLocale(Locale locale) {}
    public void setMaxSqlCacheSize(int cacheSize) {}
    public void setLockingEnabled(boolean lockingEnabled) {}

    public boolean needUpgrade(int newVersion) { return newVersion > getVersion(); }

    public boolean isDatabaseIntegrityOk() {
        checkOpen();
        try {
            return "ok".equalsIgnoreCase(
                    android.database.DatabaseUtils.stringForQuery(this, "PRAGMA integrity_check(1)", null));
        } catch (SQLiteException e) {
            return false;
        }
    }

    public List<android.util.Pair<String, String>> getAttachedDbs() {
        final ArrayList<android.util.Pair<String, String>> attached = new ArrayList<>();
        if (mHandle == 0) return null;
        attached.add(new android.util.Pair<String, String>("main", mPath));
        return attached;
    }
}
