import java.text.DecimalFormat;
import java.text.DecimalFormatSymbols;
import java.text.FieldPosition;
import java.text.ParsePosition;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.math.RoundingMode;
import java.util.Locale;

public final class NumberFormatTests {
    private static void check(boolean ok, String name) { TestSupport.check(ok, name); }
    private static DecimalFormat decimal(String pattern) {
        return new DecimalFormat(pattern, new DecimalFormatSymbols(Locale.US));
    }
    public static String run() {
        TestSupport.report.setLength(0);
        try { arithmetic(); } catch (Throwable t) { TestSupport.fail("decimal arithmetic", t); }
        try { patterns(); } catch (Throwable t) { TestSupport.fail("decimal patterns", t); }
        try { parsing(); } catch (Throwable t) { TestSupport.fail("decimal parsing", t); }
        try { symbols(); } catch (Throwable t) { TestSupport.fail("decimal symbols", t); }
        return TestSupport.report.toString();
    }
    static void arithmetic() {
        BigInteger a = new BigInteger("123456789012345678901234567890");
        BigInteger b = new BigInteger("98765432109876543210");
        check(a.multiply(b).toString().equals("12193263113702179522496570642237463801111263526900"),
                "large integer multiplication carries across words");
        BigInteger[] qr = a.divideAndRemainder(b);
        check(qr[0].toString().equals("1249999988") && qr[1].toString().equals("60185185207253086410"),
                "large integer division preserves quotient and remainder");
        check(a.negate().divide(b).equals(qr[0].negate()) && a.negate().remainder(b).equals(qr[1].negate()),
                "integer division truncates toward zero with signed remainder");
        check(new BigInteger("-128").bitLength() == 7 && new BigInteger("-129").bitLength() == 8,
                "negative integer bit length follows two's complement");
        check(new BigInteger(new byte[] {(byte) 0x80}).longValue() == -128,
                "integer constructor decodes negative two's complement");
        check(BigInteger.valueOf(Long.MIN_VALUE).toString().equals("-9223372036854775808"),
                "integer construction handles minimum long magnitude");
        check(BigInteger.valueOf(3).modInverse(BigInteger.valueOf(11)).longValue() == 4,
                "modular inverse uses exact integer arithmetic");
        check(BigInteger.valueOf(5).modPow(BigInteger.valueOf(117), BigInteger.valueOf(19)).longValue() == 1,
                "modular exponentiation reduces intermediate powers");
    }
    static void patterns() {
        DecimalFormat f = decimal("#,##0.00;(#,##0.00)");
        check("1,234.50".equals(f.format(1234.5)), "decimal grouping and required fraction digits");
        check("(1,234.50)".equals(f.format(-1234.5)), "negative subpattern preserves affixes");
        check("9,223,372,036,854,775,807.00".equals(f.format(Long.MAX_VALUE)), "long formatting preserves all digits");
        check("(9,223,372,036,854,775,808.00)".equals(f.format(Long.MIN_VALUE)), "minimum long formats without overflow");
        f = decimal("0.##");
        check("0".equals(f.format(0)) && "1.25".equals(f.format(1.25)), "optional fraction digits trim zeroes");
        check("-0".equals(f.format(-0.0)), "negative zero keeps its sign");
        f = decimal("0.0");
        check("1.2".equals(f.format(new BigDecimal("1.25"))) &&
                "1.4".equals(f.format(new BigDecimal("1.35"))), "default decimal rounding uses half even");
        f.setRoundingMode(RoundingMode.FLOOR);
        check("-1.3".equals(f.format(new BigDecimal("-1.21"))), "directed rounding uses the signed value");
        f = decimal("0%");
        check("13%".equals(f.format(new BigDecimal("0.1251"))), "percent pattern scales before rounding");
        f = decimal("0.0\u2030");
        check("125.0\u2030".equals(f.format(0.125)), "per mille pattern scales by one thousand");
        f = decimal("'size '000.00' MB'");
        check("size 003.50 MB".equals(f.format(3.5)), "quoted affixes and minimum integer digits");
        check("1.".equals(decimal("0.").format(1)), "trailing decimal separator is preserved");
        check(".5".equals(decimal(".##").format(0.5)), "fraction-only patterns omit the zero integer");
        check("1.23E04".equals(decimal("0.00E00").format(12345)), "scientific exponent and mantissa precision");
        check("12.34E3".equals(decimal("##0.0E0").format(12345)), "engineering notation uses significant digits");
        check("1.00E1".equals(decimal("0.00E0").format(new BigDecimal("9.999"))), "scientific rounding carries into exponent");
        FieldPosition field = new FieldPosition(DecimalFormat.FRACTION_FIELD);
        StringBuffer buffer = new StringBuffer("x ");
        decimal("0.00").format(12.5, buffer, field);
        check(field.getBeginIndex() == 5 && field.getEndIndex() == 7, "fraction field position accounts for buffer prefix");
        String[] invalid = {"0.0.0", "0#", "0E", "0,", "'unterminated0", "0.0#0"};
        for (String pattern : invalid) {
            try { decimal(pattern); check(false, "reject malformed decimal pattern " + pattern); }
            catch (IllegalArgumentException expected) { check(true, "reject malformed decimal pattern " + pattern); }
        }
    }
    static void parsing() {
        DecimalFormat f = decimal("#,##0.00;(#,##0.00)");
        ParsePosition p = new ParsePosition(2);
        Number n = f.parse("x (1,234.50) tail", p);
        check(n != null && n.doubleValue() == -1234.5 && p.getIndex() == 12, "parse affixes and grouping from an offset");
        p = new ParsePosition(0);
        n = decimal("0%").parse("12.5%", p);
        check(n != null && n.doubleValue() == 0.125 && p.getIndex() == 5, "parse reverses percent scaling");
        f = decimal("0.##"); f.setParseBigDecimal(true);
        p = new ParsePosition(0);
        n = f.parse("123456789012345678901234567890123456789.25", p);
        check(n instanceof BigDecimal && n.toString().equals("123456789012345678901234567890123456789.25"),
                "BigDecimal parsing retains arbitrary precision");
        f.setMultiplier(3); p = new ParsePosition(0);
        n = f.parse("1.00", p);
        check(n != null && "0.33".equals(n.toString()), "repeating multiplier division keeps parsed decimal scale");
        f.setRoundingMode(RoundingMode.FLOOR); p = new ParsePosition(0);
        n = f.parse("-1.00", p);
        check(n != null && "-0.33".equals(n.toString()), "parsed sign is applied after multiplier rounding");
        f.setMultiplier(-3); p = new ParsePosition(0); n = f.parse("-1.00", p);
        check(n != null && "0.34".equals(n.toString()), "negative multiplier participates in decimal rounding");
        f.setMultiplier(3);
        f.setParseBigDecimal(false); p = new ParsePosition(0);
        n = f.parse("1", p);
        check(n != null && n.doubleValue() == 1.0 / 3.0, "ordinary repeating division returns a double");
        p = new ParsePosition(0);
        n = decimal("0").parse("9223372036854775807", p);
        check(n instanceof Long && n.longValue() == Long.MAX_VALUE, "integral parsing returns an exact long");
        p = new ParsePosition(0);
        n = decimal("0E0").parse("1.25E-3tail", p);
        check(n != null && n.doubleValue() == 0.00125 && p.getIndex() == 7, "scientific parsing stops before trailing text");
        p = new ParsePosition(0);
        n = decimal("0;0-").parse("12-", p);
        check(n != null && n.longValue() == -12 && p.getIndex() == 3, "negative suffix disambiguates identical prefixes");
        p = new ParsePosition(0);
        n = decimal("0' kg'").parse("12 lb", p);
        check(n == null && p.getIndex() == 0 && p.getErrorIndex() >= 0, "failed suffix leaves parse index unchanged");
    }
    static void symbols() {
        DecimalFormatSymbols symbols = new DecimalFormatSymbols(Locale.US);
        symbols.setDecimalSeparator(','); symbols.setGroupingSeparator('_');
        DecimalFormat f = new DecimalFormat("#,##0.00", symbols);
        symbols.setDecimalSeparator('.');
        check("1_234,50".equals(f.format(1234.5)), "formatter defensively copies its symbols");
        DecimalFormatSymbols copy = f.getDecimalFormatSymbols();
        copy.setZeroDigit('\u0660'); f.setDecimalFormatSymbols(copy);
        check("\u0661_\u0662\u0663\u0664,\u0665\u0660".equals(f.format(1234.5)), "custom zero digit localizes all digits");
        f = decimal("0%");
        copy = f.getDecimalFormatSymbols(); copy.setPercent('p'); copy.setMinusSign('~');
        f.setDecimalFormatSymbols(copy);
        check("~50p".equals(f.format(-0.5)), "changing symbols refreshes patterned signs and affixes");
        f = decimal("#,##0.00;(#,##0.00)");
        check(f.equals((DecimalFormat) f.clone()), "decimal clone keeps formatter configuration");
        DecimalFormat restored = decimal(f.toPattern());
        check(restored.format(-1234.5).equals(f.format(-1234.5)), "decimal pattern round trip keeps formatting");
        copy = f.getDecimalFormatSymbols(); copy.setDecimalSeparator(','); copy.setGroupingSeparator('.');
        f.setDecimalFormatSymbols(copy);
        restored = new DecimalFormat("0", copy); restored.applyLocalizedPattern(f.toLocalizedPattern());
        check(restored.format(-1234.5).equals(f.format(-1234.5)), "localized decimal pattern round trip");
    }
}
