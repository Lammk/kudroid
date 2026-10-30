package android.text.format;

import android.content.Context;

public class DateUtils {
    public static final long SECOND_IN_MILLIS = 1000;
    public static final long MINUTE_IN_MILLIS = SECOND_IN_MILLIS * 60;
    public static final long HOUR_IN_MILLIS = MINUTE_IN_MILLIS * 60;
    public static final long DAY_IN_MILLIS = HOUR_IN_MILLIS * 24;
    public static final long WEEK_IN_MILLIS = DAY_IN_MILLIS * 7;
    public static final long YEAR_IN_MILLIS = WEEK_IN_MILLIS * 52;

    public static final int FORMAT_SHOW_TIME = 0x00001;
    public static final int FORMAT_SHOW_WEEKDAY = 0x00002;
    public static final int FORMAT_SHOW_YEAR = 0x00004;
    public static final int FORMAT_SHOW_DATE = 0x00010;
    public static final int FORMAT_NO_YEAR = 0x00008;
    public static final int FORMAT_ABBREV_ALL = 0x80000;

    private DateUtils() {}

    public static String formatElapsedTime(long elapsedSeconds) {
        return formatElapsedTime(null, elapsedSeconds);
    }

    public static String formatElapsedTime(StringBuilder recycle, long elapsedSeconds) {
        long hours = 0, minutes = 0, seconds = 0;
        if (elapsedSeconds >= 3600) {
            hours = elapsedSeconds / 3600;
            elapsedSeconds -= hours * 3600;
        }
        if (elapsedSeconds >= 60) {
            minutes = elapsedSeconds / 60;
            elapsedSeconds -= minutes * 60;
        }
        seconds = elapsedSeconds;
        final StringBuilder sb = recycle != null ? recycle : new StringBuilder(8);
        sb.setLength(0);
        if (hours > 0) {
            sb.append(hours).append(':');
            if (minutes < 10) sb.append('0');
        }
        sb.append(minutes).append(':');
        if (seconds < 10) sb.append('0');
        sb.append(seconds);
        return sb.toString();
    }

    public static boolean isToday(long when) {
        final java.util.Calendar now = java.util.Calendar.getInstance();
        final java.util.Calendar then = java.util.Calendar.getInstance();
        then.setTimeInMillis(when);
        return now.get(java.util.Calendar.YEAR) == then.get(java.util.Calendar.YEAR)
                && now.get(java.util.Calendar.DAY_OF_YEAR) == then.get(java.util.Calendar.DAY_OF_YEAR);
    }

    public static String formatDateTime(Context context, long millis, int flags) {
        final StringBuilder pattern = new StringBuilder();
        if ((flags & FORMAT_SHOW_WEEKDAY) != 0) pattern.append("EEEE, ");
        if ((flags & FORMAT_SHOW_DATE) != 0) {
            pattern.append((flags & FORMAT_ABBREV_ALL) != 0 ? "MMM d" : "MMMM d");
            if ((flags & FORMAT_SHOW_YEAR) != 0) pattern.append(", yyyy");
            pattern.append(' ');
        }
        if ((flags & FORMAT_SHOW_TIME) != 0 || pattern.length() == 0) pattern.append("HH:mm");
        return DateFormat.format(pattern.toString().trim(), millis).toString();
    }

    public static CharSequence getRelativeTimeSpanString(long millis) {
        return getRelativeTimeSpanString(millis, System.currentTimeMillis(), MINUTE_IN_MILLIS);
    }

    public static CharSequence getRelativeTimeSpanString(long time, long now, long minResolution) {
        final long duration = Math.abs(now - time);
        final boolean past = now >= time;
        final String text;
        if (duration < MINUTE_IN_MILLIS) {
            final long n = duration / SECOND_IN_MILLIS;
            text = n + (n == 1 ? " second" : " seconds");
        } else if (duration < HOUR_IN_MILLIS) {
            final long n = duration / MINUTE_IN_MILLIS;
            text = n + (n == 1 ? " minute" : " minutes");
        } else if (duration < DAY_IN_MILLIS) {
            final long n = duration / HOUR_IN_MILLIS;
            text = n + (n == 1 ? " hour" : " hours");
        } else {
            final long n = duration / DAY_IN_MILLIS;
            text = n + (n == 1 ? " day" : " days");
        }
        return past ? text + " ago" : "in " + text;
    }
}
