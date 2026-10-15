package java.text;

import java.util.Calendar;
import java.util.Date;
import java.util.GregorianCalendar;
import java.util.Locale;
import java.util.TimeZone;

/**
 * java.text.SimpleDateFormat — pattern-based date formatting and parsing.
 *
 * Written for KuDroid rather than ported: Apache Harmony's version needs
 * com.ibm.icu.text.DecimalFormat through NumberFormat, and AOSP's is a thin shim over
 * ICU as well. This implements the pattern letters directly against
 * GregorianCalendar.
 *
 * Supported letters (count controls zero-padding, as in the JDK):
 *   G era      y year        M month     d day-of-month   E day-of-week
 *   H hour0-23 k hour1-24    h hour1-12  K hour0-11       a AM/PM
 *   m minute   s second      S millis    D day-of-year    F day-of-week-in-month
 *   w week-of-year  W week-of-month      z/Z time zone
 * Quoted literals ('...' and '' for a single quote) are handled.
 *
 * Unsupported letters are emitted verbatim rather than throwing, so an exotic pattern
 * degrades to slightly wrong text instead of taking down the app that used it.
 */
public class SimpleDateFormat extends DateFormat {

    private static final String PATTERN_CHARS = "GyMdkHmsSEDFwWahKzZ";

    private String pattern;
    private DateFormatSymbols formatData;
    private int creationYear;
    private Date defaultCenturyStart;

    public SimpleDateFormat() {
        this("MMM d, yyyy h:mm:ss a");
    }

    public SimpleDateFormat(String template) {
        this(template, new DateFormatSymbols());
    }

    public SimpleDateFormat(String template, Locale locale) {
        this(template, new DateFormatSymbols(locale));
    }

    public SimpleDateFormat(String template, DateFormatSymbols value) {
        if (template == null) throw new NullPointerException("pattern == null");
        validatePattern(template);
        pattern = template;
        formatData = value;
        calendar = new GregorianCalendar();
        calendar.setTimeInMillis(System.currentTimeMillis());
        creationYear = calendar.get(Calendar.YEAR);
        numberFormat = NumberFormat.getIntegerInstance();
        numberFormat.setGroupingUsed(false);
    }

    /** Reject an unterminated quote; every other input is formattable. */
    private static void validatePattern(String template) {
        boolean quoted = false;
        for (int i = 0; i < template.length(); i++) {
            if (template.charAt(i) == '\'') quoted = !quoted;
        }
        if (quoted) {
            throw new IllegalArgumentException("Unterminated quote in pattern: " + template);
        }
    }

    public void applyPattern(String template) {
        validatePattern(template);
        pattern = template;
    }

    public String toPattern() {
        return pattern;
    }

    public DateFormatSymbols getDateFormatSymbols() {
        return (DateFormatSymbols) formatData.clone();
    }

    public void setDateFormatSymbols(DateFormatSymbols value) {
        if (value != null) formatData = (DateFormatSymbols) value.clone();
    }

    public Date get2DigitYearStart() {
        if (defaultCenturyStart == null) {
            Calendar c = new GregorianCalendar();
            c.clear();
            c.set(Calendar.YEAR, creationYear - 80);
            defaultCenturyStart = c.getTime();
        }
        return defaultCenturyStart;
    }

    public void set2DigitYearStart(Date date) {
        defaultCenturyStart = date;
        Calendar c = new GregorianCalendar();
        c.setTime(date);
        creationYear = c.get(Calendar.YEAR) + 80;
    }

    // Formatting.

    @Override
    public StringBuffer format(Date date, StringBuffer buffer, FieldPosition field) {
        if (date == null) throw new NullPointerException("date == null");
        calendar.setTime(date);

        int i = 0;
        while (i < pattern.length()) {
            final char c = pattern.charAt(i);

            if (c == '\'') {
                // '' inside a quote is a literal quote; otherwise the quote toggles.
                i++;
                while (i < pattern.length()) {
                    final char q = pattern.charAt(i);
                    if (q == '\'') {
                        if (i + 1 < pattern.length() && pattern.charAt(i + 1) == '\'') {
                            buffer.append('\'');
                            i += 2;
                            continue;
                        }
                        i++;
                        break;
                    }
                    buffer.append(q);
                    i++;
                }
                continue;
            }

            if (PATTERN_CHARS.indexOf(c) < 0) {
                buffer.append(c);
                i++;
                continue;
            }

            int count = 0;
            while (i + count < pattern.length() && pattern.charAt(i + count) == c) count++;
            appendField(buffer, c, count, field);
            i += count;
        }
        return buffer;
    }

    private void appendField(StringBuffer buffer, char tag, int count,
                             FieldPosition position) {
        final int begin = buffer.length();
        int fieldId = -1;

        switch (tag) {
            case 'G': {
                fieldId = DateFormat.ERA_FIELD;
                final String[] eras = formatData.getEras();
                final int era = calendar.get(Calendar.ERA);
                buffer.append(era >= 0 && era < eras.length ? eras[era] : "AD");
                break;
            }
            case 'y': {
                fieldId = DateFormat.YEAR_FIELD;
                final int year = calendar.get(Calendar.YEAR);
                // "yy" means the last two digits; any other count is zero-padded.
                if (count == 2) {
                    appendPadded(buffer, year % 100, 2);
                } else {
                    appendPadded(buffer, year, count);
                }
                break;
            }
            case 'M': {
                fieldId = DateFormat.MONTH_FIELD;
                final int month = calendar.get(Calendar.MONTH);
                if (count >= 4) {
                    final String[] names = formatData.getMonths();
                    buffer.append(month < names.length ? names[month] : "");
                } else if (count == 3) {
                    final String[] names = formatData.getShortMonths();
                    buffer.append(month < names.length ? names[month] : "");
                } else {
                    appendPadded(buffer, month + 1, count);
                }
                break;
            }
            case 'd':
                fieldId = DateFormat.DATE_FIELD;
                appendPadded(buffer, calendar.get(Calendar.DAY_OF_MONTH), count);
                break;
            case 'E': {
                fieldId = DateFormat.DAY_OF_WEEK_FIELD;
                final int dow = calendar.get(Calendar.DAY_OF_WEEK);
                final String[] names = count >= 4 ? formatData.getWeekdays()
                                                  : formatData.getShortWeekdays();
                buffer.append(dow >= 0 && dow < names.length ? names[dow] : "");
                break;
            }
            case 'a': {
                fieldId = DateFormat.AM_PM_FIELD;
                final String[] ampm = formatData.getAmPmStrings();
                final int index = calendar.get(Calendar.AM_PM);
                buffer.append(index >= 0 && index < ampm.length ? ampm[index] : "");
                break;
            }
            case 'H':
                fieldId = DateFormat.HOUR_OF_DAY0_FIELD;
                appendPadded(buffer, calendar.get(Calendar.HOUR_OF_DAY), count);
                break;
            case 'k': {
                fieldId = DateFormat.HOUR_OF_DAY1_FIELD;
                final int h = calendar.get(Calendar.HOUR_OF_DAY);
                appendPadded(buffer, h == 0 ? 24 : h, count);
                break;
            }
            case 'h': {
                fieldId = DateFormat.HOUR1_FIELD;
                final int h = calendar.get(Calendar.HOUR);
                appendPadded(buffer, h == 0 ? 12 : h, count);
                break;
            }
            case 'K':
                fieldId = DateFormat.HOUR0_FIELD;
                appendPadded(buffer, calendar.get(Calendar.HOUR), count);
                break;
            case 'm':
                fieldId = DateFormat.MINUTE_FIELD;
                appendPadded(buffer, calendar.get(Calendar.MINUTE), count);
                break;
            case 's':
                fieldId = DateFormat.SECOND_FIELD;
                appendPadded(buffer, calendar.get(Calendar.SECOND), count);
                break;
            case 'S':
                fieldId = DateFormat.MILLISECOND_FIELD;
                appendPadded(buffer, calendar.get(Calendar.MILLISECOND), count);
                break;
            case 'D':
                fieldId = DateFormat.DAY_OF_YEAR_FIELD;
                appendPadded(buffer, calendar.get(Calendar.DAY_OF_YEAR), count);
                break;
            case 'F':
                fieldId = DateFormat.DAY_OF_WEEK_IN_MONTH_FIELD;
                appendPadded(buffer, calendar.get(Calendar.DAY_OF_WEEK_IN_MONTH), count);
                break;
            case 'w':
                fieldId = DateFormat.WEEK_OF_YEAR_FIELD;
                appendPadded(buffer, calendar.get(Calendar.WEEK_OF_YEAR), count);
                break;
            case 'W':
                fieldId = DateFormat.WEEK_OF_MONTH_FIELD;
                appendPadded(buffer, calendar.get(Calendar.WEEK_OF_MONTH), count);
                break;
            case 'z':
                fieldId = DateFormat.TIMEZONE_FIELD;
                buffer.append(calendar.getTimeZone().getID());
                break;
            case 'Z':
                fieldId = DateFormat.TIMEZONE_FIELD;
                appendRfc822Zone(buffer);
                break;
            default:
                for (int i = 0; i < count; i++) buffer.append(tag);
                break;
        }

        if (position != null && fieldId != -1 && position.getField() == fieldId) {
            position.setBeginIndex(begin);
            position.setEndIndex(buffer.length());
        }
    }

    /** RFC 822 zone, e.g. +0700. */
    private void appendRfc822Zone(StringBuffer buffer) {
        int offset = calendar.get(Calendar.ZONE_OFFSET) + calendar.get(Calendar.DST_OFFSET);
        buffer.append(offset < 0 ? '-' : '+');
        if (offset < 0) offset = -offset;
        final int minutes = offset / 60000;
        appendPadded(buffer, minutes / 60, 2);
        appendPadded(buffer, minutes % 60, 2);
    }

    private static void appendPadded(StringBuffer buffer, int value, int width) {
        String digits = Integer.toString(value < 0 ? -value : value);
        if (value < 0) buffer.append('-');
        for (int i = digits.length(); i < width; i++) buffer.append('0');
        buffer.append(digits);
    }

    // Parsing.

    @Override
    public Date parse(String string) throws ParseException {
        ParsePosition position = new ParsePosition(0);
        Date result = parse(string, position);
        if (result == null) {
            throw new ParseException("Unparseable date: " + string,
                                     position.getErrorIndex());
        }
        return result;
    }

    @Override
    public Date parse(String string, ParsePosition position) {
        if (string == null || position == null) return null;

        Calendar cal = new GregorianCalendar(calendar.getTimeZone());
        cal.clear();
        cal.setLenient(calendar.isLenient());

        int text = position.getIndex();
        int i = 0;
        boolean sawAnyField = false;
        int pmFlag = -1;
        int hour12 = -1;

        while (i < pattern.length()) {
            final char c = pattern.charAt(i);

            if (c == '\'') {
                i++;
                while (i < pattern.length()) {
                    final char q = pattern.charAt(i);
                    if (q == '\'') {
                        if (i + 1 < pattern.length() && pattern.charAt(i + 1) == '\'') {
                            if (text < string.length() && string.charAt(text) == '\'') text++;
                            i += 2;
                            continue;
                        }
                        i++;
                        break;
                    }
                    if (text < string.length() && string.charAt(text) == q) text++;
                    i++;
                }
                continue;
            }

            if (PATTERN_CHARS.indexOf(c) < 0) {
                // Literal: skip it in the input if present, tolerate whitespace drift.
                if (text < string.length() && string.charAt(text) == c) {
                    text++;
                } else if (c == ' ') {
                    while (text < string.length() && string.charAt(text) == ' ') text++;
                }
                i++;
                continue;
            }

            int count = 0;
            while (i + count < pattern.length() && pattern.charAt(i + count) == c) count++;
            i += count;

            switch (c) {
                case 'y': {
                    final int[] out = new int[2];
                    if (!readNumber(string, text, count == 2 ? 2 : 10, out)) {
                        position.setErrorIndex(text);
                        return null;
                    }
                    int year = out[0];
                    text = out[1];
                    // Two-digit years resolve into the 80-year window ending 20 years out.
                    if (count == 2 && year < 100) {
                        final int base = creationYear - 80;
                        year = base - (base % 100) + year;
                        if (year < base) year += 100;
                    }
                    cal.set(Calendar.YEAR, year);
                    sawAnyField = true;
                    break;
                }
                case 'M': {
                    if (count >= 3) {
                        int month = matchName(string, text,
                                count >= 4 ? formatData.getMonths()
                                           : formatData.getShortMonths());
                        if (month < 0) {
                            position.setErrorIndex(text);
                            return null;
                        }
                        text += (count >= 4 ? formatData.getMonths()
                                            : formatData.getShortMonths())[month].length();
                        cal.set(Calendar.MONTH, month);
                    } else {
                        final int[] out = new int[2];
                        if (!readNumber(string, text, 2, out)) {
                            position.setErrorIndex(text);
                            return null;
                        }
                        cal.set(Calendar.MONTH, out[0] - 1);
                        text = out[1];
                    }
                    sawAnyField = true;
                    break;
                }
                case 'E': {
                    final String[] names = count >= 4 ? formatData.getWeekdays()
                                                      : formatData.getShortWeekdays();
                    final int dow = matchName(string, text, names);
                    if (dow >= 0) {
                        text += names[dow].length();
                        cal.set(Calendar.DAY_OF_WEEK, dow);
                    }
                    break;
                }
                case 'a': {
                    final int index = matchName(string, text, formatData.getAmPmStrings());
                    if (index >= 0) {
                        text += formatData.getAmPmStrings()[index].length();
                        pmFlag = index;
                    }
                    break;
                }
                case 'z':
                case 'Z': {
                    text = skipZone(string, text, cal);
                    break;
                }
                default: {
                    final int[] out = new int[2];
                    if (!readNumber(string, text, 10, out)) {
                        position.setErrorIndex(text);
                        return null;
                    }
                    final int value = out[0];
                    text = out[1];
                    sawAnyField = true;
                    switch (c) {
                        case 'd': cal.set(Calendar.DAY_OF_MONTH, value); break;
                        case 'H': cal.set(Calendar.HOUR_OF_DAY, value); break;
                        case 'k': cal.set(Calendar.HOUR_OF_DAY, value == 24 ? 0 : value); break;
                        case 'h': hour12 = (value == 12 ? 0 : value); break;
                        case 'K': hour12 = value; break;
                        case 'm': cal.set(Calendar.MINUTE, value); break;
                        case 's': cal.set(Calendar.SECOND, value); break;
                        case 'S': cal.set(Calendar.MILLISECOND, value); break;
                        case 'D': cal.set(Calendar.DAY_OF_YEAR, value); break;
                        case 'F': cal.set(Calendar.DAY_OF_WEEK_IN_MONTH, value); break;
                        case 'w': cal.set(Calendar.WEEK_OF_YEAR, value); break;
                        case 'W': cal.set(Calendar.WEEK_OF_MONTH, value); break;
                        case 'G': break;
                        default: break;
                    }
                    break;
                }
            }
        }

        // A 12-hour clock only resolves once AM/PM is known, so apply it at the end.
        if (hour12 >= 0) {
            cal.set(Calendar.HOUR_OF_DAY, hour12 + (pmFlag == Calendar.PM ? 12 : 0));
        }

        if (!sawAnyField) {
            position.setErrorIndex(position.getIndex());
            return null;
        }

        position.setIndex(text);
        try {
            return cal.getTime();
        } catch (Throwable t) {
            position.setErrorIndex(text);
            return null;
        }
    }

    /** Read up to maxDigits digits; out[0] = value, out[1] = new offset. */
    private static boolean readNumber(String s, int start, int maxDigits, int[] out) {
        int i = start;
        // Leading whitespace is tolerated: patterns and input often disagree on it.
        while (i < s.length() && s.charAt(i) == ' ') i++;
        boolean negative = false;
        if (i < s.length() && s.charAt(i) == '-') {
            negative = true;
            i++;
        }
        int value = 0;
        int digits = 0;
        while (i < s.length() && digits < maxDigits) {
            final char c = s.charAt(i);
            if (c < '0' || c > '9') break;
            value = value * 10 + (c - '0');
            i++;
            digits++;
        }
        if (digits == 0) return false;
        out[0] = negative ? -value : value;
        out[1] = i;
        return true;
    }

    /** Index of the longest name in `names` that the input starts with, or -1. */
    private static int matchName(String s, int start, String[] names) {
        int best = -1;
        int bestLen = 0;
        for (int i = 0; i < names.length; i++) {
            final String name = names[i];
            if (name == null || name.length() == 0) continue;
            if (name.length() <= bestLen) continue;
            if (regionMatchesIgnoreCase(s, start, name)) {
                best = i;
                bestLen = name.length();
            }
        }
        return best;
    }

    private static boolean regionMatchesIgnoreCase(String s, int start, String name) {
        if (start + name.length() > s.length()) return false;
        for (int i = 0; i < name.length(); i++) {
            final char a = Character.toLowerCase(s.charAt(start + i));
            final char b = Character.toLowerCase(name.charAt(i));
            if (a != b) return false;
        }
        return true;
    }

    /**
     * Consume a zone: "GMT+07:00", "+0700", "Z" or a bare id. Only numeric offsets change
     * the calendar; a named zone is skipped because there is no zone database to look it
     * up in, leaving the formatter's own zone in effect.
     */
    private static int skipZone(String s, int start, Calendar cal) {
        int i = start;
        if (i < s.length() && (s.charAt(i) == 'Z')) {
            cal.setTimeZone(TimeZone.getTimeZone("UTC"));
            return i + 1;
        }
        if (i + 2 < s.length() && s.regionMatches(i, "GMT", 0, 3)) {
            i += 3;
        } else if (i + 2 < s.length() && s.regionMatches(i, "UTC", 0, 3)) {
            cal.setTimeZone(TimeZone.getTimeZone("UTC"));
            return i + 3;
        }
        if (i < s.length() && (s.charAt(i) == '+' || s.charAt(i) == '-')) {
            final int sign = s.charAt(i) == '-' ? -1 : 1;
            i++;
            int hours = 0;
            int minutes = 0;
            int digits = 0;
            while (i < s.length() && digits < 2 && s.charAt(i) >= '0' && s.charAt(i) <= '9') {
                hours = hours * 10 + (s.charAt(i) - '0');
                i++;
                digits++;
            }
            if (i < s.length() && s.charAt(i) == ':') i++;
            digits = 0;
            while (i < s.length() && digits < 2 && s.charAt(i) >= '0' && s.charAt(i) <= '9') {
                minutes = minutes * 10 + (s.charAt(i) - '0');
                i++;
                digits++;
            }
            final int offset = sign * (hours * 3600000 + minutes * 60000);
            cal.setTimeZone(TimeZone.getTimeZone(offsetToGmtId(offset)));
            return i;
        }
        // Named zone: skip letters and '/'.
        while (i < s.length()) {
            final char c = s.charAt(i);
            final boolean isIdChar = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                    || c == '/' || c == '_';
            if (!isIdChar) break;
            i++;
        }
        return i;
    }

    private static String offsetToGmtId(int offsetMillis) {
        final int sign = offsetMillis < 0 ? -1 : 1;
        final int abs = offsetMillis < 0 ? -offsetMillis : offsetMillis;
        final int minutes = abs / 60000;
        StringBuffer sb = new StringBuffer("GMT");
        sb.append(sign < 0 ? '-' : '+');
        appendPadded(sb, minutes / 60, 2);
        sb.append(':');
        appendPadded(sb, minutes % 60, 2);
        return sb.toString();
    }

    @Override
    public boolean equals(Object object) {
        if (this == object) return true;
        if (!(object instanceof SimpleDateFormat)) return false;
        SimpleDateFormat other = (SimpleDateFormat) object;
        return super.equals(other) && pattern.equals(other.pattern)
                && formatData.equals(other.formatData);
    }

    @Override
    public int hashCode() {
        return super.hashCode() + pattern.hashCode() + formatData.hashCode();
    }
}
