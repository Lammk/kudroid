package java.text;

import java.math.BigDecimal;
import java.math.BigInteger;
import java.math.RoundingMode;

/** Decimal patterns with grouping, affixes, scaling and scientific notation. */
public class DecimalFormat extends NumberFormat {
    private DecimalFormatSymbols symbols;
    private String positivePrefix = "", positiveSuffix = "", negativePrefix = "-", negativeSuffix = "";
    private String positivePrefixPattern, positiveSuffixPattern, negativePrefixPattern, negativeSuffixPattern;
    private int multiplier = 1, groupingSize = 3, exponentDigits;
    private boolean decimalAlwaysShown, parseBigDecimal, currencyPattern;
    private RoundingMode roundingMode = RoundingMode.HALF_EVEN;

    public DecimalFormat() { this("#,##0.###"); }
    public DecimalFormat(String pattern) { this(pattern, new DecimalFormatSymbols()); }
    public DecimalFormat(String pattern, DecimalFormatSymbols symbols) {
        setDecimalFormatSymbols(symbols);
        applyPattern(pattern);
    }

    private static IllegalArgumentException invalidPattern(String pattern) {
        return new IllegalArgumentException("Malformed decimal pattern: " + pattern);
    }
    private static int separator(String pattern) {
        boolean quoted = false;
        for (int i = 0; i < pattern.length(); i++) {
            char c = pattern.charAt(i);
            if (c == '\'') {
                if (i + 1 < pattern.length() && pattern.charAt(i + 1) == '\'') i++;
                else quoted = !quoted;
            } else if (!quoted && c == ';') return i;
        }
        if (quoted) throw invalidPattern(pattern);
        return -1;
    }
    private String[] subpattern(String pattern, boolean positive) {
        boolean quoted = false;
        int start = -1, end = -1;
        for (int i = 0; i < pattern.length(); i++) {
            char c = pattern.charAt(i);
            if (c == '\'') {
                if (i + 1 < pattern.length() && pattern.charAt(i + 1) == '\'') i++;
                else quoted = !quoted;
            } else if (!quoted && (c == '#' || c == '0' || c == '.')) { start = i; break; }
            else if (!quoted && c == ';') throw invalidPattern(pattern);
        }
        if (start < 0) throw invalidPattern(pattern);
        end = start;
        while (end < pattern.length()) {
            char c = pattern.charAt(end);
            if (c != '#' && c != '0' && c != '.' && c != ',' && c != 'E') break;
            end++;
        }
        String number = pattern.substring(start, end);
        int exp = number.indexOf('E');
        if (exp >= 0) {
            if (number.indexOf('E', exp + 1) >= 0 || exp == number.length() - 1) throw invalidPattern(pattern);
            for (int i = exp + 1; i < number.length(); i++) if (number.charAt(i) != '0') throw invalidPattern(pattern);
        }
        String mantissa = exp < 0 ? number : number.substring(0, exp);
        int dot = mantissa.indexOf('.');
        if (dot >= 0 && mantissa.indexOf('.', dot + 1) >= 0) throw invalidPattern(pattern);
        String integer = dot < 0 ? mantissa : mantissa.substring(0, dot);
        String fraction = dot < 0 ? "" : mantissa.substring(dot + 1);
        int minInt = 0, minFrac = 0, maxInt = 0;
        boolean required = false, optional = false;
        for (int i = 0; i < integer.length(); i++) {
            char c = integer.charAt(i);
            if (c == ',') {
                if (i == integer.length() - 1 || exp >= 0)
                    throw invalidPattern(pattern);
            } else {
                if (c == '0') { required = true; minInt++; }
                else if (c != '#' || required) throw invalidPattern(pattern);
                maxInt++;
            }
        }
        for (int i = 0; i < fraction.length(); i++) {
            char c = fraction.charAt(i);
            if (c == '#') optional = true;
            else if (c == '0' && !optional) minFrac++;
            else throw invalidPattern(pattern);
        }
        if (maxInt + fraction.length() == 0) throw invalidPattern(pattern);
        if (positive) {
            exponentDigits = exp < 0 ? 0 : number.length() - exp - 1;
            setMaximumIntegerDigits(exp < 0 ? 309 : maxInt);
            setMinimumIntegerDigits(minInt == 0 && fraction.length() == 0 ? 1 : minInt);
            setMaximumFractionDigits(fraction.length()); setMinimumFractionDigits(minFrac);
            setGroupingUsed(integer.indexOf(',') >= 0);
            if (isGroupingUsed()) groupingSize = integer.length() - integer.lastIndexOf(',') - 1;
            decimalAlwaysShown = dot >= 0 && (integer.length() == 0 || fraction.length() == 0);
        }
        String prefix = pattern.substring(0, start), suffix = pattern.substring(end);
        return new String[] {affix(prefix, positive), affix(suffix, positive), prefix, suffix};
    }
    private String affix(String text, boolean positive) {
        StringBuffer out = new StringBuffer();
        boolean quoted = false;
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c == '\'') {
                if (i + 1 < text.length() && text.charAt(i + 1) == '\'') { out.append(c); i++; }
                else quoted = !quoted;
            } else if (!quoted && (c == '%' || c == '\u2030')) {
                if (positive) {
                    if (multiplier != 1) throw invalidPattern(text);
                    multiplier = c == '%' ? 100 : 1000;
                }
                out.append(c == '%' ? symbols.getPercent() : symbols.getPerMill());
            } else if (!quoted && c == '\u00a4') {
                currencyPattern = true;
                if (i + 1 < text.length() && text.charAt(i + 1) == c) {
                    out.append(symbols.getInternationalCurrencySymbol()); i++;
                } else out.append(symbols.getCurrencySymbol());
            } else if (!quoted && c == '-') out.append(symbols.getMinusSign());
            else if (!quoted && (c == ';' || c == '#' || c == '0' || c == ',' || c == '.' || c == 'E'))
                throw invalidPattern(text);
            else out.append(c);
        }
        if (quoted) throw invalidPattern(text);
        return out.toString();
    }
    public void applyPattern(String pattern) {
        if (pattern == null) throw new NullPointerException("pattern");
        int split = separator(pattern);
        multiplier = 1; currencyPattern = false;
        String[] positive = subpattern(split < 0 ? pattern : pattern.substring(0, split), true);
        positivePrefix = positive[0]; positiveSuffix = positive[1];
        positivePrefixPattern = positive[2]; positiveSuffixPattern = positive[3];
        if (split < 0 || split == pattern.length() - 1) {
            negativePrefix = symbols.getMinusSign() + positivePrefix; negativeSuffix = positiveSuffix;
            negativePrefixPattern = "-" + positivePrefixPattern; negativeSuffixPattern = positiveSuffixPattern;
        } else {
            String[] negative = subpattern(pattern.substring(split + 1), false);
            negativePrefix = negative[0]; negativeSuffix = negative[1];
            negativePrefixPattern = negative[2]; negativeSuffixPattern = negative[3];
            if (negativePrefix.equals(positivePrefix) && negativeSuffix.equals(positiveSuffix)) {
                negativePrefix = symbols.getMinusSign() + positivePrefix;
                negativePrefixPattern = "-" + positivePrefixPattern;
            }
        }
    }
    private static String quoteAffix(String text) {
        return text.length() == 0 ? "" : "'" + text.replace("'", "''") + "'";
    }
    public String toPattern() {
        StringBuffer number = new StringBuffer();
        int count = exponentDigits > 0 ? getMaximumIntegerDigits() :
                Math.max(getMinimumIntegerDigits(), isGroupingUsed() && groupingSize > 0 ? groupingSize + 1 : 1);
        for (int i = 0; i < count; i++) {
            if (i > 0 && isGroupingUsed() && groupingSize > 0 && (count - i) % groupingSize == 0) number.append(',');
            number.append(i < count - getMinimumIntegerDigits() ? '#' : '0');
        }
        if (getMaximumFractionDigits() > 0 || decimalAlwaysShown) number.append('.');
        for (int i = 0; i < getMaximumFractionDigits(); i++) number.append(i < getMinimumFractionDigits() ? '0' : '#');
        if (exponentDigits > 0) {
            number.append('E'); for (int i = 0; i < exponentDigits; i++) number.append('0');
        }
        String pattern = (positivePrefixPattern == null ? quoteAffix(positivePrefix) : positivePrefixPattern) + number +
                (positiveSuffixPattern == null ? quoteAffix(positiveSuffix) : positiveSuffixPattern);
        if (!negativePrefix.equals(symbols.getMinusSign() + positivePrefix) || !negativeSuffix.equals(positiveSuffix))
            pattern += ";" + (negativePrefixPattern == null ? quoteAffix(negativePrefix) : negativePrefixPattern) + number +
                    (negativeSuffixPattern == null ? quoteAffix(negativeSuffix) : negativeSuffixPattern);
        return pattern;
    }
    private String localizedPattern(String pattern, boolean toLocalized) {
        String plain = "0#.,;%\u2030-";
        String localized = "" + symbols.getZeroDigit() + symbols.getDigit() + symbols.getDecimalSeparator() +
                symbols.getGroupingSeparator() + symbols.getPatternSeparator() + symbols.getPercent() +
                symbols.getPerMill() + symbols.getMinusSign();
        StringBuffer out = new StringBuffer(); boolean quoted = false;
        for (int i = 0; i < pattern.length(); i++) {
            char c = pattern.charAt(i);
            if (c == '\'') {
                out.append(c);
                if (i + 1 < pattern.length() && pattern.charAt(i + 1) == c) { out.append(c); i++; }
                else quoted = !quoted;
            } else if (!quoted && toLocalized && c == 'E') out.append(symbols.getExponentSeparator());
            else if (!quoted && !toLocalized && symbols.getExponentSeparator().length() > 0 &&
                    pattern.startsWith(symbols.getExponentSeparator(), i)) {
                out.append('E'); i += symbols.getExponentSeparator().length() - 1;
            } else {
                int index = quoted ? -1 : (toLocalized ? plain : localized).indexOf(c);
                out.append(index < 0 ? c : (toLocalized ? localized : plain).charAt(index));
            }
        }
        return out.toString();
    }
    public String toLocalizedPattern() { return localizedPattern(toPattern(), true); }
    public void applyLocalizedPattern(String pattern) { applyPattern(localizedPattern(pattern, false)); }

    @Override public StringBuffer format(Object object, StringBuffer buffer, FieldPosition field) {
        if (!(object instanceof Number)) throw new IllegalArgumentException("not a Number");
        Number number = (Number) object;
        boolean floating = number instanceof Double || number instanceof Float;
        double value = number.doubleValue();
        if (floating && Double.isNaN(value)) { buffer.append(symbols.getNaN()); return buffer; }
        boolean negative = floating ? Double.doubleToLongBits(value) < 0 :
                object instanceof BigDecimal ? ((BigDecimal) object).signum() < 0 :
                object instanceof BigInteger ? ((BigInteger) object).signum() < 0 : number.longValue() < 0;
        if (multiplier < 0) negative = !negative;
        buffer.append(negative ? negativePrefix : positivePrefix);
        if (floating && Double.isInfinite(value)) buffer.append(symbols.getInfinity());
        else {
            BigDecimal decimal = object instanceof BigDecimal ? (BigDecimal) object :
                    object instanceof BigInteger ? new BigDecimal((BigInteger) object) :
                    floating ? new BigDecimal(value) : BigDecimal.valueOf(number.longValue());
            decimal = decimal.multiply(BigDecimal.valueOf(multiplier));
            int exponent = 0;
            if (exponentDigits != 0 && decimal.signum() != 0) {
                int magnitude = decimal.precision() - decimal.scale() - 1;
                int maxInt = Math.max(1, getMaximumIntegerDigits());
                if (maxInt > getMinimumIntegerDigits() && maxInt > 1) {
                    exponent = magnitude - ((magnitude % maxInt + maxInt) % maxInt);
                } else exponent = magnitude - Math.max(1, getMinimumIntegerDigits()) + 1;
                decimal = decimal.movePointLeft(exponent);
            }
            int fractionDigits = getMaximumFractionDigits();
            if (exponentDigits != 0 && getMaximumIntegerDigits() > getMinimumIntegerDigits())
                fractionDigits = Math.max(0, getMaximumIntegerDigits() + fractionDigits -
                        (decimal.precision() - decimal.scale()));
            decimal = decimal.setScale(fractionDigits, roundingMode);
            if (exponentDigits != 0 && decimal.abs().compareTo(
                    BigDecimal.ONE.movePointRight(Math.max(1, getMaximumIntegerDigits()))) >= 0) {
                int shift = Math.max(1, getMaximumIntegerDigits());
                decimal = decimal.movePointLeft(shift).setScale(getMaximumFractionDigits(), roundingMode);
                exponent += shift;
            }
            appendDecimal(decimal.abs().toPlainString(), buffer, field);
            if (exponentDigits != 0) {
                buffer.append(symbols.getExponentSeparator());
                if (exponent < 0) buffer.append(symbols.getMinusSign());
                String digits = Integer.toString(Math.abs(exponent));
                for (int i = digits.length(); i < exponentDigits; i++) buffer.append(symbols.getZeroDigit());
                appendDigits(digits, buffer);
            }
        }
        buffer.append(negative ? negativeSuffix : positiveSuffix);
        return buffer;
    }
    public StringBuffer format(double value, StringBuffer buffer, FieldPosition field) {
        return format(Double.valueOf(value), buffer, field);
    }
    public StringBuffer format(long value, StringBuffer buffer, FieldPosition field) {
        return format(Long.valueOf(value), buffer, field);
    }
    private void appendDigits(String digits, StringBuffer buffer) {
        for (int i = 0; i < digits.length(); i++) buffer.append((char) (symbols.getZeroDigit() + digits.charAt(i) - '0'));
    }
    private void appendDecimal(String text, StringBuffer buffer, FieldPosition field) {
        int dot = text.indexOf('.');
        String integer = dot < 0 ? text : text.substring(0, dot);
        String fraction = dot < 0 ? "" : text.substring(dot + 1);
        while (fraction.length() > getMinimumFractionDigits() && fraction.endsWith("0"))
            fraction = fraction.substring(0, fraction.length() - 1);
        if (integer.length() > getMaximumIntegerDigits())
            integer = integer.substring(integer.length() - getMaximumIntegerDigits());
        while (integer.length() < getMinimumIntegerDigits()) integer = "0" + integer;
        if (integer.equals("0") && getMinimumIntegerDigits() == 0 && fraction.length() > 0) integer = "";
        int begin = buffer.length();
        for (int i = 0; i < integer.length(); i++) {
            if (i > 0 && isGroupingUsed() && groupingSize > 0 && (integer.length() - i) % groupingSize == 0)
                buffer.append(symbols.getGroupingSeparator());
            buffer.append((char) (symbols.getZeroDigit() + integer.charAt(i) - '0'));
        }
        if (field != null) {
            field.setBeginIndex(0); field.setEndIndex(0);
            if (field.getField() == INTEGER_FIELD || field.getFieldAttribute() == NumberFormat.Field.INTEGER) {
                field.setBeginIndex(begin); field.setEndIndex(buffer.length());
            }
        }
        if (decimalAlwaysShown || fraction.length() > 0)
            buffer.append(currencyPattern ? symbols.getMonetaryDecimalSeparator() : symbols.getDecimalSeparator());
        begin = buffer.length(); appendDigits(fraction, buffer);
        if (field != null && (field.getField() == FRACTION_FIELD || field.getFieldAttribute() == NumberFormat.Field.FRACTION)) {
            field.setBeginIndex(begin); field.setEndIndex(buffer.length());
        }
    }

    @Override public Number parse(String text, ParsePosition position) {
        ParsePosition positive = new ParsePosition(position.getIndex());
        ParsePosition negative = new ParsePosition(position.getIndex());
        Number pos = parse(text, positive, false), neg = parse(text, negative, true);
        if (pos != null && neg != null && positive.getIndex() == negative.getIndex()) {
            position.setErrorIndex(position.getIndex()); return null;
        }
        if (neg != null && (pos == null || negative.getIndex() > positive.getIndex())) {
            position.setIndex(negative.getIndex()); return neg;
        }
        if (pos != null) { position.setIndex(positive.getIndex()); return pos; }
        position.setErrorIndex(Math.max(positive.getErrorIndex(), negative.getErrorIndex())); return null;
    }
    private Number parse(String text, ParsePosition position, boolean negative) {
        if (text == null) throw new NullPointerException("text");
        int start = position.getIndex();
        if (start < 0 || start >= text.length()) { position.setErrorIndex(start); return null; }
        if (!negative && text.startsWith(symbols.getNaN(), start)) {
            position.setIndex(start + symbols.getNaN().length()); return Double.valueOf(Double.NaN);
        }
        String prefix = negative ? negativePrefix : positivePrefix, suffix = negative ? negativeSuffix : positiveSuffix;
        if (!text.startsWith(prefix, start)) { position.setErrorIndex(start); return null; }
        int i = start + prefix.length();
        if (text.startsWith(symbols.getInfinity(), i)) {
            i += symbols.getInfinity().length();
            if (!text.startsWith(suffix, i)) { position.setErrorIndex(i); return null; }
            position.setIndex(i + suffix.length());
            return Double.valueOf(negative != (multiplier < 0) ? Double.NEGATIVE_INFINITY : Double.POSITIVE_INFINITY);
        }
        StringBuffer digits = new StringBuffer();
        boolean decimal = false, hasDigit = false;
        char separator = currencyPattern ? symbols.getMonetaryDecimalSeparator() : symbols.getDecimalSeparator();
        while (i < text.length()) {
            char c = text.charAt(i);
            int digit = c - symbols.getZeroDigit();
            if (digit < 0 || digit > 9) digit = Character.digit(c, 10);
            if (digit >= 0 && digit <= 9) { digits.append((char) ('0' + digit)); hasDigit = true; i++; }
            else if (!decimal && !isParseIntegerOnly() && c == separator) { digits.append('.'); decimal = true; i++; }
            else if (!decimal && isGroupingUsed() && c == symbols.getGroupingSeparator()) {
                if (i + 1 >= text.length() || Character.digit(text.charAt(i + 1), 10) < 0) break;
                i++;
            } else break;
        }
        if (hasDigit && text.startsWith(symbols.getExponentSeparator(), i)) {
            int j = i + symbols.getExponentSeparator().length();
            StringBuffer exponent = new StringBuffer();
            if (j < text.length() && (text.charAt(j) == symbols.getMinusSign() || text.charAt(j) == '+')) {
                exponent.append(text.charAt(j) == '+' ? '+' : '-'); j++;
            }
            int exponentStart = j;
            while (j < text.length() && Character.digit(text.charAt(j), 10) >= 0)
                exponent.append((char) ('0' + Character.digit(text.charAt(j++), 10)));
            if (j > exponentStart) { digits.append('E').append(exponent); i = j; }
        }
        if (!hasDigit) { position.setErrorIndex(start); return null; }
        if (!text.startsWith(suffix, i)) { position.setErrorIndex(i); return null; }
        try {
            BigDecimal result = new BigDecimal(digits.toString());
            if (multiplier == 0) {
                position.setIndex(i + suffix.length());
                return Double.valueOf(result.signum() == 0 ? Double.NaN :
                        negative ? Double.NEGATIVE_INFINITY : Double.POSITIVE_INFINITY);
            }
            try { result = result.divide(BigDecimal.valueOf(multiplier)); }
            catch (ArithmeticException e) {
                if (!parseBigDecimal) {
                    position.setIndex(i + suffix.length());
                    return Double.valueOf((negative ? -result.doubleValue() : result.doubleValue()) / multiplier);
                }
                result = result.divide(BigDecimal.valueOf(multiplier), roundingMode);
            }
            // Apply the affix sign after multiplier rounding.
            if (negative) result = result.negate();
            position.setIndex(i + suffix.length());
            if (parseBigDecimal) return result;
            if (negative != (multiplier < 0) && result.signum() == 0 && !isParseIntegerOnly()) return Double.valueOf(-0.0);
            try { return Long.valueOf(result.longValueExact()); }
            catch (ArithmeticException e) { return Double.valueOf(result.doubleValue()); }
        } catch (NumberFormatException e) { position.setErrorIndex(start); return null; }
    }
    public DecimalFormatSymbols getDecimalFormatSymbols() { return (DecimalFormatSymbols) symbols.clone(); }
    public void setDecimalFormatSymbols(DecimalFormatSymbols value) {
        if (value == null) throw new NullPointerException("symbols"); symbols = (DecimalFormatSymbols) value.clone();
        if (positivePrefixPattern != null) positivePrefix = affix(positivePrefixPattern, false);
        if (positiveSuffixPattern != null) positiveSuffix = affix(positiveSuffixPattern, false);
        if (negativePrefixPattern != null) negativePrefix = affix(negativePrefixPattern, false);
        if (negativeSuffixPattern != null) negativeSuffix = affix(negativeSuffixPattern, false);
    }
    public String getPositivePrefix() { return positivePrefix; }
    public void setPositivePrefix(String value) { if (value == null) throw new NullPointerException(); positivePrefix = value; positivePrefixPattern = null; }
    public String getNegativePrefix() { return negativePrefix; }
    public void setNegativePrefix(String value) { if (value == null) throw new NullPointerException(); negativePrefix = value; negativePrefixPattern = null; }
    public String getPositiveSuffix() { return positiveSuffix; }
    public void setPositiveSuffix(String value) { if (value == null) throw new NullPointerException(); positiveSuffix = value; positiveSuffixPattern = null; }
    public String getNegativeSuffix() { return negativeSuffix; }
    public void setNegativeSuffix(String value) { if (value == null) throw new NullPointerException(); negativeSuffix = value; negativeSuffixPattern = null; }
    public int getMultiplier() { return multiplier; }
    public void setMultiplier(int value) { multiplier = value; }
    public int getGroupingSize() { return groupingSize; }
    public void setGroupingSize(int value) { if (value < 0 || value > 127) throw new IllegalArgumentException(); groupingSize = value; }
    public boolean isDecimalSeparatorAlwaysShown() { return decimalAlwaysShown; }
    public void setDecimalSeparatorAlwaysShown(boolean value) { decimalAlwaysShown = value; }
    public boolean isParseBigDecimal() { return parseBigDecimal; }
    public void setParseBigDecimal(boolean value) { parseBigDecimal = value; }
    public RoundingMode getRoundingMode() { return roundingMode; }
    public void setRoundingMode(RoundingMode value) { if (value == null) throw new NullPointerException(); roundingMode = value; }
    public java.util.Currency getCurrency() { return symbols.getCurrency(); }
    public void setCurrency(java.util.Currency value) {
        DecimalFormatSymbols copy = getDecimalFormatSymbols(); copy.setCurrency(value); setDecimalFormatSymbols(copy);
    }
    @Override public boolean equals(Object object) {
        if (!(object instanceof DecimalFormat) || !super.equals(object)) return false;
        DecimalFormat other = (DecimalFormat) object;
        return multiplier == other.multiplier && groupingSize == other.groupingSize && exponentDigits == other.exponentDigits &&
                decimalAlwaysShown == other.decimalAlwaysShown && parseBigDecimal == other.parseBigDecimal &&
                currencyPattern == other.currencyPattern && roundingMode == other.roundingMode && symbols.equals(other.symbols) &&
                positivePrefix.equals(other.positivePrefix) && negativePrefix.equals(other.negativePrefix) &&
                positiveSuffix.equals(other.positiveSuffix) && negativeSuffix.equals(other.negativeSuffix);
    }
    @Override public int hashCode() { return super.hashCode() + positivePrefix.hashCode() + negativePrefix.hashCode() + symbols.hashCode(); }
    @Override public Object clone() {
        DecimalFormat copy = (DecimalFormat) super.clone();
        copy.symbols = (DecimalFormatSymbols) symbols.clone(); return copy;
    }
}
