package java.util;

/**
 * java.util.GregorianCalendar — proleptic Gregorian calendar.
 *
 * The date arithmetic is written here rather than ported from Apache Harmony because
 * Harmony's version reaches into com.ibm.icu for locale week rules, which KuDroid does
 * not ship. The conversion between a millisecond instant and calendar fields uses the
 * standard civil-from-days algorithm (Howard Hinnant's), which is exact for all years
 * and needs no lookup tables.
 *
 * "Proleptic" means the Gregorian rules are applied to dates before the 1582 cutover
 * too. The JDK switches to Julian there; apps formatting timestamps never touch that
 * range, and getting the modern range exactly right matters far more.
 */
public class GregorianCalendar extends Calendar {

    public static final int BC = 0;
    public static final int AD = 1;

    private static final long MS_PER_SECOND = 1000L;
    private static final long MS_PER_MINUTE = 60L * MS_PER_SECOND;
    private static final long MS_PER_HOUR = 60L * MS_PER_MINUTE;
    private static final long MS_PER_DAY = 24L * MS_PER_HOUR;

    public GregorianCalendar() {
        super();
    }

    public GregorianCalendar(TimeZone timezone) {
        super(timezone);
    }

    public GregorianCalendar(Locale locale) {
        super(TimeZone.getDefault(), locale);
    }

    public GregorianCalendar(TimeZone timezone, Locale locale) {
        super(timezone, locale);
    }

    public GregorianCalendar(int year, int month, int day) {
        super();
        clear();
        set(year, month, day);
    }

    public GregorianCalendar(int year, int month, int day, int hour, int minute) {
        super();
        clear();
        set(year, month, day, hour, minute);
    }

    public GregorianCalendar(int year, int month, int day, int hour, int minute,
                             int second) {
        super();
        clear();
        set(year, month, day, hour, minute, second);
    }

    // ── conversion helpers ──

    /**
     * Days since 1970-01-01 for a civil date. Exact for any year; y/m/d may be out of
     * range and will be normalised by the caller's own arithmetic.
     */
    private static long daysFromCivil(int y, int m, int d) {
        // Shift the year so that March starts the year, which removes the leap-day
        // special case from the middle of the calculation.
        int year = y;
        year -= m <= 2 ? 1 : 0;
        final int era = (year >= 0 ? year : year - 399) / 400;
        final int yoe = year - era * 400;                       // [0, 399]
        final int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
        final int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;   // [0, 146096]
        return (long) era * 146097L + doe - 719468L;
    }

    /** Inverse of daysFromCivil; writes year, month (1-12) and day into out[]. */
    private static void civilFromDays(long z, int[] out) {
        z += 719468L;
        final long era = (z >= 0 ? z : z - 146096L) / 146097L;
        final long doe = z - era * 146097L;                      // [0, 146096]
        final long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        final long y = yoe + era * 400L;
        final long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        final long mp = (5 * doy + 2) / 153;                     // [0, 11]
        final long d = doy - (153 * mp + 2) / 5 + 1;             // [1, 31]
        final long m = mp + (mp < 10 ? 3 : -9);                  // [1, 12]
        out[0] = (int) (y + (m <= 2 ? 1 : 0));
        out[1] = (int) m;
        out[2] = (int) d;
    }

    private static boolean isLeap(int year) {
        return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    }

    private static int daysInMonth(int year, int month0) {
        switch (month0) {
            case FEBRUARY: return isLeap(year) ? 29 : 28;
            case APRIL: case JUNE: case SEPTEMBER: case NOVEMBER: return 30;
            default: return 31;
        }
    }

    /** Floor division, so negative instants map to the correct earlier day. */
    private static long floorDiv(long a, long b) {
        long q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
        return q;
    }

    private static long floorMod(long a, long b) {
        return a - floorDiv(a, b) * b;
    }

    // ── Calendar contract ──

    @Override
    protected void computeTime() {
        int year = isSet(YEAR) ? fields[YEAR] : 1970;
        int month = isSet(MONTH) ? fields[MONTH] : JANUARY;
        int day = isSet(DAY_OF_MONTH) ? fields[DAY_OF_MONTH] : 1;

        // A month outside 0-11 rolls into the year, which is what set(MONTH, 13) means.
        year += floorDiv(month, 12);
        month = (int) floorMod(month, 12);

        int hour = isSet(HOUR_OF_DAY) ? fields[HOUR_OF_DAY] : 0;
        if (!isSet(HOUR_OF_DAY) && isSet(HOUR)) {
            hour = fields[HOUR] + (isSet(AM_PM) && fields[AM_PM] == PM ? 12 : 0);
        }
        final int minute = isSet(MINUTE) ? fields[MINUTE] : 0;
        final int second = isSet(SECOND) ? fields[SECOND] : 0;
        final int millis = isSet(MILLISECOND) ? fields[MILLISECOND] : 0;

        final long days = daysFromCivil(year, month + 1, day);
        long local = days * MS_PER_DAY
                + hour * MS_PER_HOUR
                + minute * MS_PER_MINUTE
                + second * MS_PER_SECOND
                + millis;

        // Fields are wall-clock in this calendar's zone, so subtract the offset to get UTC.
        time = local - getTimeZone().getOffset(local);
    }

    @Override
    protected void computeFields() {
        final TimeZone tz = getTimeZone();
        final int offset = tz.getOffset(time);
        final long local = time + offset;

        final long days = floorDiv(local, MS_PER_DAY);
        long msOfDay = floorMod(local, MS_PER_DAY);

        final int[] ymd = new int[3];
        civilFromDays(days, ymd);
        final int year = ymd[0];
        final int month0 = ymd[1] - 1;
        final int dayOfMonth = ymd[2];

        fields[ERA] = year > 0 ? AD : BC;
        fields[YEAR] = year > 0 ? year : 1 - year;
        fields[MONTH] = month0;
        fields[DAY_OF_MONTH] = dayOfMonth;

        // 1970-01-01 was a Thursday; DAY_OF_WEEK is SUNDAY..SATURDAY == 1..7.
        fields[DAY_OF_WEEK] = (int) floorMod(days + 4, 7) + 1;

        int dayOfYear = 1;
        for (int m = 0; m < month0; m++) {
            dayOfYear += daysInMonth(year, m);
        }
        dayOfYear += dayOfMonth - 1;
        fields[DAY_OF_YEAR] = dayOfYear;

        fields[DAY_OF_WEEK_IN_MONTH] = (dayOfMonth - 1) / 7 + 1;
        fields[WEEK_OF_MONTH] = (dayOfMonth + 6) / 7;
        fields[WEEK_OF_YEAR] = (dayOfYear + 6) / 7;

        final int msi = (int) msOfDay;
        fields[MILLISECOND] = msi % 1000;
        final int totalSeconds = msi / 1000;
        fields[SECOND] = totalSeconds % 60;
        fields[MINUTE] = (totalSeconds / 60) % 60;
        final int hourOfDay = totalSeconds / 3600;
        fields[HOUR_OF_DAY] = hourOfDay;
        fields[AM_PM] = hourOfDay < 12 ? AM : PM;
        fields[HOUR] = hourOfDay % 12;

        fields[ZONE_OFFSET] = tz.getRawOffset();
        fields[DST_OFFSET] = offset - tz.getRawOffset();

        for (int i = 0; i < FIELD_COUNT; i++) {
            isSet[i] = true;
        }
    }

    @Override
    public void add(int field, int value) {
        if (value == 0) return;
        complete();

        switch (field) {
            case YEAR:
            case MONTH: {
                int months = fields[YEAR] * 12 + fields[MONTH];
                months += (field == YEAR) ? value * 12 : value;
                final int newYear = (int) floorDiv(months, 12);
                final int newMonth = (int) floorMod(months, 12);
                // Clamp the day: adding a month to Jan 31 lands on Feb 28/29, not Mar 3.
                final int maxDay = daysInMonth(newYear, newMonth);
                final int day = Math.min(fields[DAY_OF_MONTH], maxDay);
                fields[YEAR] = newYear;
                fields[MONTH] = newMonth;
                fields[DAY_OF_MONTH] = day;
                isTimeSet = false;
                areFieldsSet = false;
                complete();
                return;
            }
            case DATE:  // == DAY_OF_MONTH, same constant
            case DAY_OF_YEAR:
            case DAY_OF_WEEK:
                setTimeInMillis(getTimeInMillis() + value * MS_PER_DAY);
                return;
            case WEEK_OF_YEAR:
            case WEEK_OF_MONTH:
            case DAY_OF_WEEK_IN_MONTH:
                setTimeInMillis(getTimeInMillis() + value * 7L * MS_PER_DAY);
                return;
            case AM_PM:
                setTimeInMillis(getTimeInMillis() + value * 12L * MS_PER_HOUR);
                return;
            case HOUR:
            case HOUR_OF_DAY:
                setTimeInMillis(getTimeInMillis() + value * MS_PER_HOUR);
                return;
            case MINUTE:
                setTimeInMillis(getTimeInMillis() + value * MS_PER_MINUTE);
                return;
            case SECOND:
                setTimeInMillis(getTimeInMillis() + value * MS_PER_SECOND);
                return;
            case MILLISECOND:
                setTimeInMillis(getTimeInMillis() + value);
                return;
            default:
                throw new IllegalArgumentException("cannot add to field " + field);
        }
    }

    @Override
    public void roll(int field, boolean increase) {
        // roll() changes one field without carrying into larger ones, so it wraps
        // within that field's range instead of adding to the instant.
        complete();
        final int delta = increase ? 1 : -1;
        switch (field) {
            case YEAR:
                fields[YEAR] += delta;
                break;
            case MONTH:
                fields[MONTH] = (int) floorMod(fields[MONTH] + delta, 12);
                break;
            case DAY_OF_MONTH: {  // == DATE, same constant
                final int max = daysInMonth(fields[YEAR], fields[MONTH]);
                int d = fields[DAY_OF_MONTH] + delta;
                if (d > max) d = 1;
                if (d < 1) d = max;
                fields[DAY_OF_MONTH] = d;
                break;
            }
            case HOUR_OF_DAY:
                fields[HOUR_OF_DAY] = (int) floorMod(fields[HOUR_OF_DAY] + delta, 24);
                break;
            case HOUR:
                fields[HOUR] = (int) floorMod(fields[HOUR] + delta, 12);
                fields[HOUR_OF_DAY] = fields[HOUR] + (fields[AM_PM] == PM ? 12 : 0);
                break;
            case MINUTE:
                fields[MINUTE] = (int) floorMod(fields[MINUTE] + delta, 60);
                break;
            case SECOND:
                fields[SECOND] = (int) floorMod(fields[SECOND] + delta, 60);
                break;
            case MILLISECOND:
                fields[MILLISECOND] = (int) floorMod(fields[MILLISECOND] + delta, 1000);
                break;
            case AM_PM:
                fields[AM_PM] = fields[AM_PM] == AM ? PM : AM;
                fields[HOUR_OF_DAY] = fields[HOUR] + (fields[AM_PM] == PM ? 12 : 0);
                break;
            default:
                return;
        }
        isTimeSet = false;
        areFieldsSet = false;
        complete();
    }

    public boolean isLeapYear(int year) {
        return isLeap(year);
    }

    @Override
    public int getMinimum(int field) {
        switch (field) {
            case ERA: return BC;
            case YEAR: return 1;
            case MONTH: return JANUARY;
            case DAY_OF_MONTH: return 1;
            case DAY_OF_YEAR: return 1;
            case DAY_OF_WEEK: return SUNDAY;
            case DAY_OF_WEEK_IN_MONTH: return 1;
            case WEEK_OF_MONTH: return 1;
            case WEEK_OF_YEAR: return 1;
            case ZONE_OFFSET: return -13 * (int) MS_PER_HOUR;
            case DST_OFFSET: return 0;
            default: return 0;
        }
    }

    @Override
    public int getMaximum(int field) {
        switch (field) {
            case ERA: return AD;
            case YEAR: return 292278994;
            case MONTH: return DECEMBER;
            case DAY_OF_MONTH: return 31;
            case DAY_OF_YEAR: return 366;
            case DAY_OF_WEEK: return SATURDAY;
            case DAY_OF_WEEK_IN_MONTH: return 6;
            case WEEK_OF_MONTH: return 6;
            case WEEK_OF_YEAR: return 53;
            case AM_PM: return PM;
            case HOUR: return 11;
            case HOUR_OF_DAY: return 23;
            case MINUTE: return 59;
            case SECOND: return 59;
            case MILLISECOND: return 999;
            case ZONE_OFFSET: return 14 * (int) MS_PER_HOUR;
            case DST_OFFSET: return (int) MS_PER_HOUR;
            default: return 0;
        }
    }

    @Override
    public int getGreatestMinimum(int field) {
        return getMinimum(field);
    }

    @Override
    public int getLeastMaximum(int field) {
        switch (field) {
            case DAY_OF_MONTH: return 28;
            case DAY_OF_YEAR: return 365;
            case WEEK_OF_MONTH: return 4;
            case WEEK_OF_YEAR: return 52;
            case DAY_OF_WEEK_IN_MONTH: return 4;
            default: return getMaximum(field);
        }
    }

    @Override
    public int getActualMaximum(int field) {
        complete();
        switch (field) {
            case DAY_OF_MONTH: return daysInMonth(fields[YEAR], fields[MONTH]);
            case DAY_OF_YEAR: return isLeap(fields[YEAR]) ? 366 : 365;
            default: return getMaximum(field);
        }
    }
}
