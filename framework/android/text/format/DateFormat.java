package android.text.format;

import android.content.Context;

import java.util.Calendar;
import java.util.Date;
import java.util.Locale;

/** Date and time formatting with the platform's pattern letters. */
public class DateFormat {
    public static final char AM_PM = 'a';
    public static final char CAPITAL_AM_PM = 'A';
    public static final char DATE = 'd';
    public static final char DAY = 'E';
    public static final char HOUR = 'h';
    public static final char HOUR_OF_DAY = 'k';
    public static final char MINUTE = 'm';
    public static final char MONTH = 'M';
    public static final char QUARTER = 'Q';
    public static final char SECONDS = 's';
    public static final char STANDALONE_MONTH = 'L';
    public static final char TIME_ZONE = 'z';
    public static final char YEAR = 'y';

    public DateFormat() {}

    public static boolean is24HourFormat(Context context) {
        return true;
    }

    public static boolean is24HourFormat(Context context, int userHandle) {
        return true;
    }

    public static java.text.DateFormat getTimeFormat(Context context) {
        return new java.text.SimpleDateFormat(is24HourFormat(context) ? "HH:mm" : "h:mm a",
                Locale.getDefault());
    }

    public static String getTimeFormatString(Context context) {
        return is24HourFormat(context) ? "HH:mm" : "h:mm a";
    }

    public static java.text.DateFormat getDateFormat(Context context) {
        return java.text.DateFormat.getDateInstance(java.text.DateFormat.SHORT);
    }

    public static java.text.DateFormat getLongDateFormat(Context context) {
        return java.text.DateFormat.getDateInstance(java.text.DateFormat.LONG);
    }

    public static java.text.DateFormat getMediumDateFormat(Context context) {
        return java.text.DateFormat.getDateInstance(java.text.DateFormat.MEDIUM);
    }

    public static char[] getDateFormatOrder(Context context) {
        return new char[] {'M', 'd', 'y'};
    }

    public static String getBestDateTimePattern(Locale locale, String skeleton) {
        return skeleton;
    }

    public static CharSequence format(CharSequence inFormat, long inTimeInMillis) {
        return format(inFormat, new Date(inTimeInMillis));
    }

    public static CharSequence format(CharSequence inFormat, Date inDate) {
        final Calendar c = Calendar.getInstance();
        c.setTime(inDate);
        return format(inFormat, c);
    }

    public static boolean hasSeconds(CharSequence inFormat) {
        return hasDesignator(inFormat, SECONDS);
    }

    public static boolean hasDesignator(CharSequence inFormat, char designator) {
        if (inFormat == null) return false;
        boolean quoted = false;
        for (int i = 0; i < inFormat.length(); i++) {
            final char c = inFormat.charAt(i);
            if (c == '\'') quoted = !quoted;
            else if (!quoted && c == designator) return true;
        }
        return false;
    }

    /** Pattern letters unknown to SimpleDateFormat are written out as quoted text first. */
    public static CharSequence format(CharSequence inFormat, Calendar inDate) {
        final String pattern = inFormat.toString();
        final StringBuilder out = new StringBuilder();
        boolean quoted = false;
        for (int i = 0; i < pattern.length(); i++) {
            final char c = pattern.charAt(i);
            if (c == '\'') {
                quoted = !quoted;
                out.append(c);
            } else if (quoted) {
                out.append(c);
            } else if (c == CAPITAL_AM_PM) {
                while (i + 1 < pattern.length() && pattern.charAt(i + 1) == c) i++;
                out.append('\'').append(inDate.get(Calendar.AM_PM) == Calendar.AM ? "AM" : "PM").append('\'');
            } else if (c == QUARTER) {
                while (i + 1 < pattern.length() && pattern.charAt(i + 1) == c) i++;
                out.append('\'').append(inDate.get(Calendar.MONTH) / 3 + 1).append('\'');
            } else if (c == STANDALONE_MONTH) {
                out.append(MONTH);
            } else if (c == 'c') {
                out.append(DAY);
            } else {
                out.append(c);
            }
        }
        final java.text.SimpleDateFormat f = new java.text.SimpleDateFormat(out.toString(), Locale.getDefault());
        f.setTimeZone(inDate.getTimeZone());
        return f.format(inDate.getTime());
    }
}
