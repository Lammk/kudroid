package java.text;

import java.util.Currency;
import java.util.Locale;

/** Symbols used by decimal number patterns. */
public class DecimalFormatSymbols implements Cloneable, java.io.Serializable {
    private char zeroDigit = '0', groupingSeparator = ',', decimalSeparator = '.',
            perMill = '\u2030', percent = '%', digit = '#', patternSeparator = ';', minusSign = '-';
    private String infinity = "\u221e", nan = "NaN", exponentSeparator = "E";
    private String currencySymbol, internationalCurrencySymbol;
    private char monetaryDecimalSeparator;

    public DecimalFormatSymbols() { this(Locale.getDefault(Locale.Category.FORMAT)); }
    public DecimalFormatSymbols(Locale locale) {
        if (locale == null) throw new NullPointerException("locale");
        String language = locale.getLanguage();
        if ("fr".equals(language)) { decimalSeparator = ','; groupingSeparator = '\u202f'; }
        else if ("de".equals(language) || "it".equals(language) || "es".equals(language)
                || "pt".equals(language) || "vi".equals(language)) {
            decimalSeparator = ','; groupingSeparator = '.';
        }
        monetaryDecimalSeparator = decimalSeparator;
        Currency currency = Currency.getInstance(locale);
        internationalCurrencySymbol = currency.getCurrencyCode();
        currencySymbol = currency.getSymbol(locale);
    }
    public static DecimalFormatSymbols getInstance() { return new DecimalFormatSymbols(); }
    public static DecimalFormatSymbols getInstance(Locale locale) { return new DecimalFormatSymbols(locale); }
    public static Locale[] getAvailableLocales() { return Locale.getAvailableLocales(); }
    public char getZeroDigit() { return zeroDigit; }
    public void setZeroDigit(char value) { zeroDigit = value; }
    public char getGroupingSeparator() { return groupingSeparator; }
    public void setGroupingSeparator(char value) { groupingSeparator = value; }
    public char getDecimalSeparator() { return decimalSeparator; }
    public void setDecimalSeparator(char value) { decimalSeparator = value; }
    public char getPerMill() { return perMill; }
    public void setPerMill(char value) { perMill = value; }
    public char getPercent() { return percent; }
    public void setPercent(char value) { percent = value; }
    public char getDigit() { return digit; }
    public void setDigit(char value) { digit = value; }
    public char getPatternSeparator() { return patternSeparator; }
    public void setPatternSeparator(char value) { patternSeparator = value; }
    public char getMinusSign() { return minusSign; }
    public void setMinusSign(char value) { minusSign = value; }
    public String getInfinity() { return infinity; }
    public void setInfinity(String value) { if (value == null) throw new NullPointerException(); infinity = value; }
    public String getNaN() { return nan; }
    public void setNaN(String value) { if (value == null) throw new NullPointerException(); nan = value; }
    public String getExponentSeparator() { return exponentSeparator; }
    public void setExponentSeparator(String value) { if (value == null) throw new NullPointerException(); exponentSeparator = value; }
    public String getCurrencySymbol() { return currencySymbol; }
    public void setCurrencySymbol(String value) { if (value == null) throw new NullPointerException(); currencySymbol = value; }
    public String getInternationalCurrencySymbol() { return internationalCurrencySymbol; }
    public void setInternationalCurrencySymbol(String value) { internationalCurrencySymbol = value; }
    public char getMonetaryDecimalSeparator() { return monetaryDecimalSeparator; }
    public void setMonetaryDecimalSeparator(char value) { monetaryDecimalSeparator = value; }
    public Currency getCurrency() { return Currency.getInstance(internationalCurrencySymbol); }
    public void setCurrency(Currency value) {
        if (value == null) throw new NullPointerException();
        internationalCurrencySymbol = value.getCurrencyCode(); currencySymbol = value.getSymbol();
    }
    @Override public Object clone() {
        try { return super.clone(); } catch (CloneNotSupportedException e) { throw new AssertionError(e); }
    }
    @Override public boolean equals(Object object) {
        if (!(object instanceof DecimalFormatSymbols)) return false;
        DecimalFormatSymbols other = (DecimalFormatSymbols) object;
        return zeroDigit == other.zeroDigit && groupingSeparator == other.groupingSeparator &&
                decimalSeparator == other.decimalSeparator && perMill == other.perMill && percent == other.percent &&
                digit == other.digit && patternSeparator == other.patternSeparator && minusSign == other.minusSign &&
                monetaryDecimalSeparator == other.monetaryDecimalSeparator && infinity.equals(other.infinity) &&
                nan.equals(other.nan) && exponentSeparator.equals(other.exponentSeparator) &&
                currencySymbol.equals(other.currencySymbol) &&
                (internationalCurrencySymbol == null ? other.internationalCurrencySymbol == null :
                        internationalCurrencySymbol.equals(other.internationalCurrencySymbol));
    }
    @Override public int hashCode() { return zeroDigit + groupingSeparator * 31 + decimalSeparator * 961; }
}
