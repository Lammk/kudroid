package android.text.format;

import android.content.Context;

public final class Formatter {
    private static final String[] UNITS = {"B", "kB", "MB", "GB", "TB", "PB"};

    private Formatter() {}

    public static String formatFileSize(Context context, long sizeBytes) {
        return format(sizeBytes);
    }

    public static String formatShortFileSize(Context context, long sizeBytes) {
        return format(sizeBytes);
    }

    private static String format(long sizeBytes) {
        final boolean negative = sizeBytes < 0;
        double result = negative ? -sizeBytes : sizeBytes;
        int unit = 0;
        while (result >= 1000 && unit < UNITS.length - 1) {
            result /= 1000;
            unit++;
        }
        final String number;
        if (unit == 0) number = Long.toString((long) result);
        else if (result < 10) number = String.format(java.util.Locale.getDefault(), "%.2f", result);
        else if (result < 100) number = String.format(java.util.Locale.getDefault(), "%.1f", result);
        else number = String.format(java.util.Locale.getDefault(), "%.0f", result);
        return (negative ? "-" : "") + number + " " + UNITS[unit];
    }
}
