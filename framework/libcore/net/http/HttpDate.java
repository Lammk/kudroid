package libcore.net.http;

/** Parses and formats HTTP dates without depending on locale or time zone data. */
public final class HttpDate {
    private HttpDate() {}

    private static final String[] MONTHS = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    private static final String[] DAYS = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};

    /** Milliseconds since the epoch for an RFC 1123, RFC 850 or asctime date, or -1. */
    public static long parse(String s) {
        if (s == null) return -1;
        String t = s.trim();
        if (t.length() == 0) return -1;
        try {
            // Drop a leading weekday ("Sun," / "Sunday,").
            int comma = t.indexOf(',');
            if (comma > 0) t = t.substring(comma + 1).trim();
            String[] tok = tokens(t);
            if (tok.length < 3) return -1;
            int day, month, year;
            String time;
            if (isDigit(tok[0].charAt(0))) {
                // "06 Nov 1994 08:49:37 GMT" or "06-Nov-94 08:49:37 GMT"
                String first = tok[0];
                if (first.indexOf('-') > 0) {
                    int d1 = first.indexOf('-');
                    int d2 = first.indexOf('-', d1 + 1);
                    day = Integer.parseInt(first.substring(0, d1));
                    month = monthOf(first.substring(d1 + 1, d2));
                    year = Integer.parseInt(first.substring(d2 + 1));
                    time = tok[1];
                } else {
                    day = Integer.parseInt(first);
                    month = monthOf(tok[1]);
                    year = Integer.parseInt(tok[2]);
                    time = tok[3];
                }
            } else {
                // asctime: "Sun Nov  6 08:49:37 1994" (weekday first, no comma)
                if (tok.length < 5) return -1;
                month = monthOf(tok[1]);
                day = Integer.parseInt(tok[2]);
                time = tok[3];
                year = Integer.parseInt(tok[4]);
            }
            if (month < 0) return -1;
            if (year < 100) year += year < 70 ? 2000 : 1900;
            int c1 = time.indexOf(':');
            int c2 = time.indexOf(':', c1 + 1);
            int hh = Integer.parseInt(time.substring(0, c1));
            int mm = Integer.parseInt(time.substring(c1 + 1, c2));
            int ss = Integer.parseInt(time.substring(c2 + 1));
            long days = daysFromCivil(year, month + 1, day);
            return ((days * 24 + hh) * 60 + mm) * 60000L + ss * 1000L;
        } catch (RuntimeException e) {
            return -1;
        }
    }

    private static String[] tokens(String t) {
        java.util.ArrayList<String> out = new java.util.ArrayList<String>();
        int i = 0;
        while (i < t.length()) {
            while (i < t.length() && t.charAt(i) == ' ') i++;
            int j = i;
            while (j < t.length() && t.charAt(j) != ' ') j++;
            if (j > i) out.add(t.substring(i, j));
            i = j;
        }
        return out.toArray(new String[out.size()]);
    }

    private static long floorDiv(long a, long b) {
        long q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    private static long floorMod(long a, long b) {
        return a - floorDiv(a, b) * b;
    }

    private static boolean isDigit(char c) { return c >= '0' && c <= '9'; }

    private static int monthOf(String m) {
        for (int i = 0; i < 12; i++) {
            if (m.regionMatches(0, MONTHS[i], 0, 3)) return i;
        }
        return -1;
    }

    /** Days since 1970-01-01 for a proleptic Gregorian date (month 1-12). */
    public static long daysFromCivilPublic(int y, int m, int d) {
        return daysFromCivil(y, m, d);
    }

    static long daysFromCivil(int y, int m, int d) {
        y -= m <= 2 ? 1 : 0;
        long era = (y >= 0 ? y : y - 399) / 400;
        long yoe = y - era * 400;
        long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    public static String format(long millis) {
        long secs = floorDiv(millis, 1000L);
        long days = floorDiv(secs, 86400L);
        long rem = secs - days * 86400L;
        // civil from days
        long z = days + 719468;
        long era = (z >= 0 ? z : z - 146096) / 146097;
        long doe = z - era * 146097;
        long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long y = yoe + era * 400;
        long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        long mp = (5 * doy + 2) / 153;
        long d = doy - (153 * mp + 2) / 5 + 1;
        long m = mp + (mp < 10 ? 3 : -9);
        if (m <= 2) y++;
        String wd = DAYS[(int) floorMod(days, 7L)];
        StringBuilder sb = new StringBuilder();
        sb.append(wd).append(", ");
        if (d < 10) sb.append('0');
        sb.append(d).append(' ').append(MONTHS[(int) m - 1]).append(' ').append(y).append(' ');
        two(sb, rem / 3600).append(':');
        two(sb, (rem % 3600) / 60).append(':');
        two(sb, rem % 60).append(" GMT");
        return sb.toString();
    }

    private static StringBuilder two(StringBuilder sb, long v) {
        if (v < 10) sb.append('0');
        return sb.append(v);
    }
}
