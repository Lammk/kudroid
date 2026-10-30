package android.database;

import android.database.sqlite.SQLiteDatabase;

/** On corruption the database file is left alone and the failure is reported to the log. */
public final class DefaultDatabaseErrorHandler implements DatabaseErrorHandler {
    @Override
    public void onCorruption(SQLiteDatabase dbObj) {
        android.util.Log.e("DefaultDatabaseErrorHandler", "Corruption reported by sqlite on database: "
                + (dbObj != null ? dbObj.getPath() : "?"));
    }
}
