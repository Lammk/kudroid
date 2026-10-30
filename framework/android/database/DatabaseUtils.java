package android.database;

import android.database.sqlite.SQLiteDatabase;

public class DatabaseUtils {
    private DatabaseUtils() {}

    public static String sqlEscapeString(String value) {
        final StringBuilder escaped = new StringBuilder();
        appendEscapedSQLString(escaped, value);
        return escaped.toString();
    }

    public static void appendEscapedSQLString(StringBuilder sb, String sqlString) {
        sb.append('\'');
        if (sqlString.indexOf('\'') != -1) {
            for (int i = 0; i < sqlString.length(); i++) {
                final char c = sqlString.charAt(i);
                if (c == '\'') sb.append('\'');
                sb.append(c);
            }
        } else {
            sb.append(sqlString);
        }
        sb.append('\'');
    }

    public static long queryNumEntries(SQLiteDatabase db, String table) {
        return queryNumEntries(db, table, null, null);
    }

    public static long queryNumEntries(SQLiteDatabase db, String table, String selection) {
        return queryNumEntries(db, table, selection, null);
    }

    public static long queryNumEntries(SQLiteDatabase db, String table, String selection,
            String[] selectionArgs) {
        final String where = selection != null && !selection.isEmpty() ? " where " + selection : "";
        return longForQuery(db, "select count(*) from " + table + where, selectionArgs);
    }

    public static long longForQuery(SQLiteDatabase db, String query, String[] selectionArgs) {
        final android.database.sqlite.SQLiteStatement prog = db.compileStatement(query);
        try {
            if (selectionArgs != null) prog.bindAllArgsAsStrings(selectionArgs);
            return prog.simpleQueryForLong();
        } finally {
            prog.close();
        }
    }

    public static String stringForQuery(SQLiteDatabase db, String query, String[] selectionArgs) {
        final android.database.sqlite.SQLiteStatement prog = db.compileStatement(query);
        try {
            if (selectionArgs != null) prog.bindAllArgsAsStrings(selectionArgs);
            return prog.simpleQueryForString();
        } finally {
            prog.close();
        }
    }
}
