package java.util;

/**
 * java.util.Calendar — abstract base for calendar field arithmetic.
 *
 * Written for KuDroid rather than ported: Apache Harmony's Calendar calls
 * com.ibm.icu.util.Calendar in its locale constructor, and there is no ICU on the
 * device. The field model and the get/set/add/roll contract follow the JDK, which is
 * what callers depend on; the calendar arithmetic itself lives in GregorianCalendar.
 *
 * Field values are held in a fixed array and converted to and from a millisecond
 * instant lazily, matching the JDK's behaviour where set() marks fields dirty and the
 * next get() recomputes.
 */
public abstract class Calendar implements Cloneable, Comparable<Calendar> {

    public static final int ERA = 0;
    public static final int YEAR = 1;
    public static final int MONTH = 2;
    public static final int WEEK_OF_YEAR = 3;
    public static final int WEEK_OF_MONTH = 4;
    public static final int DATE = 5;
    public static final int DAY_OF_MONTH = 5;
    public static final int DAY_OF_YEAR = 6;
    public static final int DAY_OF_WEEK = 7;
    public static final int DAY_OF_WEEK_IN_MONTH = 8;
    public static final int AM_PM = 9;
    public static final int HOUR = 10;
    public static final int HOUR_OF_DAY = 11;
    public static final int MINUTE = 12;
    public static final int SECOND = 13;
    public static final int MILLISECOND = 14;
    public static final int ZONE_OFFSET = 15;
    public static final int DST_OFFSET = 16;
    public static final int FIELD_COUNT = 17;

    public static final int AM = 0;
    public static final int PM = 1;

    public static final int SUNDAY = 1;
    public static final int MONDAY = 2;
    public static final int TUESDAY = 3;
    public static final int WEDNESDAY = 4;
    public static final int THURSDAY = 5;
    public static final int FRIDAY = 6;
    public static final int SATURDAY = 7;

    public static final int JANUARY = 0;
    public static final int FEBRUARY = 1;
    public static final int MARCH = 2;
    public static final int APRIL = 3;
    public static final int MAY = 4;
    public static final int JUNE = 5;
    public static final int JULY = 6;
    public static final int AUGUST = 7;
    public static final int SEPTEMBER = 8;
    public static final int OCTOBER = 9;
    public static final int NOVEMBER = 10;
    public static final int DECEMBER = 11;
    public static final int UNDECIMBER = 12;

    protected int[] fields = new int[FIELD_COUNT];
    protected boolean[] isSet = new boolean[FIELD_COUNT];
    protected boolean areFieldsSet;
    protected long time;
    protected boolean isTimeSet;

    private boolean lenient = true;
    private TimeZone zone;
    private int firstDayOfWeek = SUNDAY;
    private int minimalDaysInFirstWeek = 1;

    protected Calendar() {
        this(TimeZone.getDefault());
    }

    protected Calendar(TimeZone timezone) {
        zone = timezone != null ? timezone : TimeZone.getDefault();
        setTimeInMillis(System.currentTimeMillis());
    }

    protected Calendar(TimeZone timezone, Locale locale) {
        this(timezone);
    }

    public static Calendar getInstance() {
        return new GregorianCalendar();
    }

    public static Calendar getInstance(TimeZone timezone) {
        return new GregorianCalendar(timezone);
    }

    public static Calendar getInstance(Locale locale) {
        return new GregorianCalendar();
    }

    public static Calendar getInstance(TimeZone timezone, Locale locale) {
        return new GregorianCalendar(timezone);
    }

    // ── field access ──

    public int get(int field) {
        complete();
        return fields[field];
    }

    public void set(int field, int value) {
        fields[field] = value;
        isSet[field] = true;
        areFieldsSet = false;
        isTimeSet = false;
    }

    public final void set(int year, int month, int day) {
        set(YEAR, year);
        set(MONTH, month);
        set(DAY_OF_MONTH, day);
    }

    public final void set(int year, int month, int day, int hourOfDay, int minute) {
        set(year, month, day);
        set(HOUR_OF_DAY, hourOfDay);
        set(MINUTE, minute);
    }

    public final void set(int year, int month, int day, int hourOfDay, int minute,
                          int second) {
        set(year, month, day, hourOfDay, minute);
        set(SECOND, second);
    }

    public final void clear() {
        for (int i = 0; i < FIELD_COUNT; i++) {
            fields[i] = 0;
            isSet[i] = false;
        }
        areFieldsSet = false;
        isTimeSet = false;
    }

    public final void clear(int field) {
        fields[field] = 0;
        isSet[field] = false;
        areFieldsSet = false;
        isTimeSet = false;
    }

    public final boolean isSet(int field) {
        return isSet[field];
    }

    // ── time ──

    public final Date getTime() {
        return new Date(getTimeInMillis());
    }

    public final void setTime(Date date) {
        setTimeInMillis(date.getTime());
    }

    public long getTimeInMillis() {
        if (!isTimeSet) {
            computeTime();
            isTimeSet = true;
        }
        return time;
    }

    public void setTimeInMillis(long milliseconds) {
        time = milliseconds;
        isTimeSet = true;
        areFieldsSet = false;
        complete();
    }

    /** Recompute any field that is not currently valid. */
    protected void complete() {
        if (!isTimeSet) {
            computeTime();
            isTimeSet = true;
        }
        if (!areFieldsSet) {
            computeFields();
            areFieldsSet = true;
        }
    }

    protected abstract void computeTime();

    protected abstract void computeFields();

    public abstract void add(int field, int value);

    public abstract void roll(int field, boolean increase);

    public void roll(int field, int value) {
        final boolean increase = value >= 0;
        final int count = increase ? value : -value;
        for (int i = 0; i < count; i++) {
            roll(field, increase);
        }
    }

    public abstract int getMinimum(int field);

    public abstract int getMaximum(int field);

    public abstract int getGreatestMinimum(int field);

    public abstract int getLeastMaximum(int field);

    public int getActualMinimum(int field) {
        return getGreatestMinimum(field);
    }

    public int getActualMaximum(int field) {
        return getLeastMaximum(field);
    }

    // ── comparison ──

    public boolean before(Object calendar) {
        return (calendar instanceof Calendar)
                && getTimeInMillis() < ((Calendar) calendar).getTimeInMillis();
    }

    public boolean after(Object calendar) {
        return (calendar instanceof Calendar)
                && getTimeInMillis() > ((Calendar) calendar).getTimeInMillis();
    }

    @Override
    public int compareTo(Calendar other) {
        final long lhs = getTimeInMillis();
        final long rhs = other.getTimeInMillis();
        return lhs < rhs ? -1 : (lhs == rhs ? 0 : 1);
    }

    @Override
    public boolean equals(Object object) {
        if (this == object) return true;
        if (!(object instanceof Calendar)) return false;
        Calendar other = (Calendar) object;
        return getTimeInMillis() == other.getTimeInMillis()
                && lenient == other.lenient
                && firstDayOfWeek == other.firstDayOfWeek
                && minimalDaysInFirstWeek == other.minimalDaysInFirstWeek
                && zone.getID().equals(other.zone.getID());
    }

    @Override
    public int hashCode() {
        final long t = getTimeInMillis();
        return (int) (t ^ (t >>> 32));
    }

    @Override
    public Object clone() {
        try {
            Calendar copy = (Calendar) super.clone();
            copy.fields = new int[FIELD_COUNT];
            copy.isSet = new boolean[FIELD_COUNT];
            for (int i = 0; i < FIELD_COUNT; i++) {
                copy.fields[i] = fields[i];
                copy.isSet[i] = isSet[i];
            }
            return copy;
        } catch (CloneNotSupportedException e) {
            return null;
        }
    }

    // ── configuration ──

    public boolean isLenient() {
        return lenient;
    }

    public void setLenient(boolean value) {
        lenient = value;
    }

    public TimeZone getTimeZone() {
        return zone;
    }

    public void setTimeZone(TimeZone timezone) {
        if (timezone == null) return;
        zone = timezone;
        areFieldsSet = false;
    }

    public int getFirstDayOfWeek() {
        return firstDayOfWeek;
    }

    public void setFirstDayOfWeek(int value) {
        firstDayOfWeek = value;
    }

    public int getMinimalDaysInFirstWeek() {
        return minimalDaysInFirstWeek;
    }

    public void setMinimalDaysInFirstWeek(int value) {
        minimalDaysInFirstWeek = value;
    }

    @Override
    public String toString() {
        complete();
        return getClass().getName() + "[time=" + time
                + ",year=" + fields[YEAR]
                + ",month=" + fields[MONTH]
                + ",day=" + fields[DAY_OF_MONTH]
                + ",hour=" + fields[HOUR_OF_DAY]
                + ",minute=" + fields[MINUTE]
                + ",second=" + fields[SECOND] + "]";
    }
}
