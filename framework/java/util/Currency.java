package java.util;

import java.io.Serializable;

public final class Currency implements Serializable {
    private static final long serialVersionUID = -158308464356906721L;
    private final String currencyCode;

    private Currency(String currencyCode) {
        this.currencyCode = currencyCode;
    }
    public static Currency getInstance(String currencyCode) {
        return new Currency(currencyCode);
    }
    public static Currency getInstance(Locale locale) {
        return new Currency("USD");
    }
    public static Set<Currency> getAvailableCurrencies() {
        Set<Currency> s = new HashSet<Currency>();
        s.add(new Currency("USD"));
        s.add(new Currency("EUR"));
        s.add(new Currency("VND"));
        return s;
    }
    public String getCurrencyCode() { return currencyCode; }
    public String getSymbol() { return currencyCode; }
    public String getSymbol(Locale locale) { return currencyCode; }
    public int getDefaultFractionDigits() { return 2; }
    public String getDisplayName() { return currencyCode; }
    public String getDisplayName(Locale locale) { return currencyCode; }
    public String toString() { return currencyCode; }
}
