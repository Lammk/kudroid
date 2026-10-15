package java.text;

import java.util.Calendar;
import java.util.Date;
import java.util.GregorianCalendar;
import java.util.Locale;
import java.util.TimeZone;

/**
 * java.text.DateFormat — abstract base for date/time formatters.
 *
 * The style constants and the getXxxInstance() factories match the JDK so app code
 * compiles unchanged; the actual pattern work happens in SimpleDateFormat.
 */
public abstract class DateFormat extends Format {

    public static final int DEFAULT = 2;
    public static final int SHORT = 3;
    public static final int MEDIUM = 2;
    public static final int LONG = 1;
    public static final int FULL = 0;

    public static final int ERA_FIELD = 0;
    public static final int YEAR_FIELD = 1;
    public static final int MONTH_FIELD = 2;
    public static final int DATE_FIELD = 3;
    public static final int HOUR_OF_DAY1_FIELD = 4;
    public static final int HOUR_OF_DAY0_FIELD = 5;
    public static final int MINUTE_FIELD = 6;
    public static final int SECOND_FIELD = 7;
    public static final int MILLISECOND_FIELD = 8;
    public static final int DAY_OF_WEEK_FIELD = 9;
    public static final int DAY_OF_YEAR_FIELD = 10;
    public static final int DAY_OF_WEEK_IN_MONTH_FIELD = 11;
    public static final int WEEK_OF_YEAR_FIELD = 12;
    public static final int WEEK_OF_MONTH_FIELD = 13;
    public static final int AM_PM_FIELD = 14;
    public static final int HOUR1_FIELD = 15;
    public static final int HOUR0_FIELD = 16;
    public static final int TIMEZONE_FIELD = 17;

    protected Calendar calendar;
    protected NumberFormat numberFormat;

    protected DateFormat() {
    }

    public final String format(Date date) {
        return format(date, new StringBuffer(), new FieldPosition(0)).toString();
    }

    public abstract StringBuffer format(Date date, StringBuffer buffer,
                                       FieldPosition field);

    @Override
    public final StringBuffer format(Object object, StringBuffer buffer,
                                     FieldPosition field) {
        if (object instanceof Date) {
            return format((Date) object, buffer, field);
        }
        if (object instanceof Number) {
            return format(new Date(((Number) object).longValue()), buffer, field);
        }
        throw new IllegalArgumentException("not a Date: " + object);
    }

    public abstract Date parse(String string) throws ParseException;

    public abstract Date parse(String string, ParsePosition position);

    @Override
    public Object parseObject(String string, ParsePosition position) {
        return parse(string, position);
    }

    // Factories.

    public static final DateFormat getInstance() {
        return getDateTimeInstance(SHORT, SHORT);
    }

    public static final DateFormat getDateInstance() {
        return getDateInstance(DEFAULT);
    }

    public static final DateFormat getDateInstance(int style) {
        return new SimpleDateFormat(datePattern(style));
    }

    public static final DateFormat getDateInstance(int style, Locale locale) {
        return new SimpleDateFormat(datePattern(style), locale);
    }

    public static final DateFormat getTimeInstance() {
        return getTimeInstance(DEFAULT);
    }

    public static final DateFormat getTimeInstance(int style) {
        return new SimpleDateFormat(timePattern(style));
    }

    public static final DateFormat getTimeInstance(int style, Locale locale) {
        return new SimpleDateFormat(timePattern(style), locale);
    }

    public static final DateFormat getDateTimeInstance() {
        return getDateTimeInstance(DEFAULT, DEFAULT);
    }

    public static final DateFormat getDateTimeInstance(int dateStyle, int timeStyle) {
        return new SimpleDateFormat(datePattern(dateStyle) + " " + timePattern(timeStyle));
    }

    public static final DateFormat getDateTimeInstance(int dateStyle, int timeStyle,
                                                      Locale locale) {
        return new SimpleDateFormat(datePattern(dateStyle) + " " + timePattern(timeStyle),
                                    locale);
    }

    private static String datePattern(int style) {
        switch (style) {
            case FULL: return "EEEE, MMMM d, yyyy";
            case LONG: return "MMMM d, yyyy";
            case SHORT: return "M/d/yy";
            default: return "MMM d, yyyy";   // MEDIUM == DEFAULT
        }
    }

    private static String timePattern(int style) {
        switch (style) {
            case FULL:
            case LONG: return "h:mm:ss a z";
            case SHORT: return "h:mm a";
            default: return "h:mm:ss a";     // MEDIUM == DEFAULT
        }
    }

    public static Locale[] getAvailableLocales() {
        return new Locale[] { Locale.getDefault() };
    }

    public Calendar getCalendar() {
        return calendar;
    }

    public void setCalendar(Calendar cal) {
        calendar = cal;
    }

    public NumberFormat getNumberFormat() {
        return numberFormat;
    }

    public void setNumberFormat(NumberFormat format) {
        numberFormat = format;
    }

    public TimeZone getTimeZone() {
        return calendar != null ? calendar.getTimeZone() : TimeZone.getDefault();
    }

    public void setTimeZone(TimeZone timezone) {
        if (calendar != null) calendar.setTimeZone(timezone);
    }

    public boolean isLenient() {
        return calendar == null || calendar.isLenient();
    }

    public void setLenient(boolean value) {
        if (calendar != null) calendar.setLenient(value);
    }

    @Override
    public Object clone() {
        DateFormat clone = (DateFormat) super.clone();
        if (calendar != null) clone.calendar = (Calendar) calendar.clone();
        if (numberFormat != null) clone.numberFormat = (NumberFormat) numberFormat.clone();
        return clone;
    }

    @Override
    public boolean equals(Object object) {
        if (this == object) return true;
        if (!(object instanceof DateFormat)) return false;
        DateFormat other = (DateFormat) object;
        if (calendar == null ? other.calendar != null : !calendar.equals(other.calendar)) {
            return false;
        }
        return numberFormat == null ? other.numberFormat == null
                                    : numberFormat.equals(other.numberFormat);
    }

    @Override
    public int hashCode() {
        return (calendar == null ? 0 : calendar.hashCode())
                + (numberFormat == null ? 0 : numberFormat.hashCode());
    }

    /** Field identifiers for formatted output. */
    public static class Field extends Format.Field {
        public static final Field ERA = new Field("era", Calendar.ERA);
        public static final Field YEAR = new Field("year", Calendar.YEAR);
        public static final Field MONTH = new Field("month", Calendar.MONTH);
        public static final Field DAY_OF_MONTH = new Field("day of month",
                Calendar.DAY_OF_MONTH);
        public static final Field HOUR_OF_DAY1 = new Field("hour of day 1", -1);
        public static final Field HOUR_OF_DAY0 = new Field("hour of day",
                Calendar.HOUR_OF_DAY);
        public static final Field MINUTE = new Field("minute", Calendar.MINUTE);
        public static final Field SECOND = new Field("second", Calendar.SECOND);
        public static final Field MILLISECOND = new Field("millisecond",
                Calendar.MILLISECOND);
        public static final Field DAY_OF_WEEK = new Field("day of week",
                Calendar.DAY_OF_WEEK);
        public static final Field DAY_OF_YEAR = new Field("day of year",
                Calendar.DAY_OF_YEAR);
        public static final Field AM_PM = new Field("am pm", Calendar.AM_PM);
        public static final Field HOUR1 = new Field("hour 1", -1);
        public static final Field HOUR0 = new Field("hour", Calendar.HOUR);
        public static final Field TIME_ZONE = new Field("time zone", -1);

        private final int calendarField;

        protected Field(String fieldName, int calendarField) {
            super(fieldName);
            this.calendarField = calendarField;
        }

        public int getCalendarField() {
            return calendarField;
        }
    }
}
