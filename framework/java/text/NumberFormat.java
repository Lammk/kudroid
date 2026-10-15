package java.text;

import java.util.Locale;

/**
 * java.text.NumberFormat — number formatting, minimal but real.
 *
 * Apache Harmony's NumberFormat/DecimalFormat pair delegates to
 * com.ibm.icu.text.DecimalFormat, so it cannot be ported without ICU. This version
 * implements grouping, fraction digits and minimum integer digits directly, which
 * covers what DateFormat needs (zero-padded integers) and the common
 * NumberFormat.getInstance().format(n) call.
 */
public class NumberFormat extends Format {

    public static final int INTEGER_FIELD = 0;
    public static final int FRACTION_FIELD = 1;

    private boolean groupingUsed = true;
    private boolean parseIntegerOnly;
    private int maximumIntegerDigits = 40;
    private int minimumIntegerDigits = 1;
    private int maximumFractionDigits = 3;
    private int minimumFractionDigits;

    protected NumberFormat() {
    }

    public static NumberFormat getInstance() {
        return new NumberFormat();
    }

    public static NumberFormat getInstance(Locale locale) {
        return new NumberFormat();
    }

    public static NumberFormat getNumberInstance() {
        return new NumberFormat();
    }

    public static NumberFormat getNumberInstance(Locale locale) {
        return new NumberFormat();
    }

    public static NumberFormat getIntegerInstance() {
        NumberFormat format = new NumberFormat();
        format.setParseIntegerOnly(true);
        format.setMaximumFractionDigits(0);
        return format;
    }

    public static NumberFormat getIntegerInstance(Locale locale) {
        return getIntegerInstance();
    }

    public static NumberFormat getPercentInstance() {
        return new NumberFormat();
    }

    public static NumberFormat getCurrencyInstance() {
        return new NumberFormat();
    }

    public static Locale[] getAvailableLocales() {
        return new Locale[] { Locale.getDefault() };
    }

    public final String format(double value) {
        return format(Double.valueOf(value), new StringBuffer(), new FieldPosition(0))
                .toString();
    }

    public final String format(long value) {
        return format(Long.valueOf(value), new StringBuffer(), new FieldPosition(0))
                .toString();
    }

    @Override
    public StringBuffer format(Object object, StringBuffer buffer, FieldPosition field) {
        if (!(object instanceof Number)) {
            throw new IllegalArgumentException("not a Number: " + object);
        }
        final Number number = (Number) object;
        final boolean isIntegral = (number instanceof Integer) || (number instanceof Long)
                || (number instanceof Short) || (number instanceof Byte);

        if (isIntegral && maximumFractionDigits == 0) {
            appendInteger(buffer, number.longValue(), field);
            return buffer;
        }
        if (isIntegral) {
            appendInteger(buffer, number.longValue(), field);
            appendFraction(buffer, 0.0);
            return buffer;
        }

        double value = number.doubleValue();
        if (Double.isNaN(value)) {
            buffer.append("NaN");
            return buffer;
        }
        if (Double.isInfinite(value)) {
            buffer.append(value > 0 ? "\u221e" : "-\u221e");
            return buffer;
        }

        final boolean negative = value < 0;
        if (negative) value = -value;

        // Round at the last kept fraction digit before splitting, so 0.999 with two
        // fraction digits becomes 1.00 rather than 0.99.
        double scale = 1.0;
        for (int i = 0; i < maximumFractionDigits; i++) scale *= 10.0;
        final double rounded = Math.floor(value * scale + 0.5) / scale;

        final long whole = (long) rounded;
        final double frac = rounded - whole;

        if (negative) buffer.append('-');
        appendInteger(buffer, whole, field);
        appendFraction(buffer, frac);
        return buffer;
    }

    private void appendInteger(StringBuffer buffer, long value, FieldPosition field) {
        final int begin = buffer.length();
        boolean negative = value < 0;
        String digits = Long.toString(negative ? -value : value);

        if (digits.length() < minimumIntegerDigits) {
            StringBuffer pad = new StringBuffer();
            for (int i = digits.length(); i < minimumIntegerDigits; i++) pad.append('0');
            digits = pad.toString() + digits;
        }
        if (digits.length() > maximumIntegerDigits) {
            digits = digits.substring(digits.length() - maximumIntegerDigits);
        }
        if (negative) buffer.append('-');

        if (groupingUsed && digits.length() > 3) {
            final int lead = digits.length() % 3;
            if (lead > 0) {
                buffer.append(digits, 0, lead);
                if (digits.length() > lead) buffer.append(',');
            }
            for (int i = lead; i < digits.length(); i += 3) {
                buffer.append(digits, i, i + 3);
                if (i + 3 < digits.length()) buffer.append(',');
            }
        } else {
            buffer.append(digits);
        }

        if (field != null && field.getField() == INTEGER_FIELD) {
            field.setBeginIndex(begin);
            field.setEndIndex(buffer.length());
        }
    }

    private void appendFraction(StringBuffer buffer, double frac) {
        if (maximumFractionDigits <= 0) return;

        StringBuffer digits = new StringBuffer();
        double f = frac;
        for (int i = 0; i < maximumFractionDigits; i++) {
            f *= 10.0;
            final int d = (int) f;
            digits.append((char) ('0' + d));
            f -= d;
        }
        // Drop trailing zeros down to the minimum the caller asked for.
        int end = digits.length();
        while (end > minimumFractionDigits && digits.charAt(end - 1) == '0') end--;
        if (end == 0) return;
        buffer.append('.').append(digits, 0, end);
    }

    @Override
    public Object parseObject(String string, ParsePosition position) {
        return parse(string, position);
    }

    public Number parse(String string) throws ParseException {
        ParsePosition position = new ParsePosition(0);
        Number result = parse(string, position);
        if (position.getIndex() == 0) {
            throw new ParseException("Unparseable number: " + string,
                                     position.getErrorIndex());
        }
        return result;
    }

    /** Accepts an optional sign, grouping commas, and a fraction unless integer-only. */
    public Number parse(String string, ParsePosition position) {
        if (string == null) return null;
        int i = position.getIndex();
        final int len = string.length();
        final int start = i;

        boolean negative = false;
        if (i < len && (string.charAt(i) == '-' || string.charAt(i) == '+')) {
            negative = string.charAt(i) == '-';
            i++;
        }

        StringBuffer whole = new StringBuffer();
        while (i < len) {
            final char c = string.charAt(i);
            if (c >= '0' && c <= '9') {
                whole.append(c);
                i++;
            } else if (c == ',' && groupingUsed) {
                i++;
            } else {
                break;
            }
        }
        if (whole.length() == 0) {
            position.setErrorIndex(start);
            return null;
        }

        StringBuffer fraction = new StringBuffer();
        if (!parseIntegerOnly && i < len && string.charAt(i) == '.') {
            int j = i + 1;
            while (j < len && string.charAt(j) >= '0' && string.charAt(j) <= '9') {
                fraction.append(string.charAt(j));
                j++;
            }
            // Only consume the '.' when at least one digit follows it.
            if (fraction.length() > 0) i = j;
        }

        position.setIndex(i);
        final String text = (negative ? "-" : "") + whole
                + (fraction.length() > 0 ? "." + fraction : "");
        try {
            if (fraction.length() == 0) {
                return Long.valueOf(Long.parseLong(text));
            }
            return Double.valueOf(Double.parseDouble(text));
        } catch (Throwable t) {
            position.setIndex(start);
            position.setErrorIndex(start);
            return null;
        }
    }

    public boolean isGroupingUsed() {
        return groupingUsed;
    }

    public void setGroupingUsed(boolean value) {
        groupingUsed = value;
    }

    public boolean isParseIntegerOnly() {
        return parseIntegerOnly;
    }

    public void setParseIntegerOnly(boolean value) {
        parseIntegerOnly = value;
    }

    public int getMaximumIntegerDigits() {
        return maximumIntegerDigits;
    }

    public void setMaximumIntegerDigits(int value) {
        maximumIntegerDigits = value < 0 ? 0 : value;
        if (minimumIntegerDigits > maximumIntegerDigits) {
            minimumIntegerDigits = maximumIntegerDigits;
        }
    }

    public int getMinimumIntegerDigits() {
        return minimumIntegerDigits;
    }

    public void setMinimumIntegerDigits(int value) {
        minimumIntegerDigits = value < 0 ? 0 : value;
        if (maximumIntegerDigits < minimumIntegerDigits) {
            maximumIntegerDigits = minimumIntegerDigits;
        }
    }

    public int getMaximumFractionDigits() {
        return maximumFractionDigits;
    }

    public void setMaximumFractionDigits(int value) {
        maximumFractionDigits = value < 0 ? 0 : value;
        if (minimumFractionDigits > maximumFractionDigits) {
            minimumFractionDigits = maximumFractionDigits;
        }
    }

    public int getMinimumFractionDigits() {
        return minimumFractionDigits;
    }

    public void setMinimumFractionDigits(int value) {
        minimumFractionDigits = value < 0 ? 0 : value;
        if (maximumFractionDigits < minimumFractionDigits) {
            maximumFractionDigits = minimumFractionDigits;
        }
    }

    @Override
    public boolean equals(Object object) {
        if (this == object) return true;
        if (!(object instanceof NumberFormat)) return false;
        NumberFormat other = (NumberFormat) object;
        return groupingUsed == other.groupingUsed
                && parseIntegerOnly == other.parseIntegerOnly
                && maximumIntegerDigits == other.maximumIntegerDigits
                && minimumIntegerDigits == other.minimumIntegerDigits
                && maximumFractionDigits == other.maximumFractionDigits
                && minimumFractionDigits == other.minimumFractionDigits;
    }

    @Override
    public int hashCode() {
        return (groupingUsed ? 1 : 0) + (parseIntegerOnly ? 2 : 0)
                + maximumIntegerDigits + minimumIntegerDigits
                + maximumFractionDigits + minimumFractionDigits;
    }

    /** Field identifiers for formatted numbers. */
    public static class Field extends Format.Field {
        public static final Field INTEGER = new Field("integer");
        public static final Field FRACTION = new Field("fraction");
        public static final Field SIGN = new Field("sign");
        public static final Field GROUPING_SEPARATOR = new Field("grouping separator");
        public static final Field DECIMAL_SEPARATOR = new Field("decimal separator");

        protected Field(String fieldName) {
            super(fieldName);
        }
    }
}
