package java.util;

public final class Locale {

    public static final Locale US = new Locale("en", "US");
    public static final Locale UK = new Locale("en", "GB");
    public static final Locale ENGLISH = new Locale("en", "");
    public static final Locale ROOT = new Locale("", "");

    private static Locale defaultLocale = US;

    private final String language;
    private final String country;

    public Locale(String language) {
        this(language, "");
    }

    public Locale(String language, String country) {
        this.language = language == null ? "" : language;
        this.country = country == null ? "" : country;
    }

    public String getLanguage() {
        return language;
    }

    public String getCountry() {
        return country;
    }

    public String getDisplayLanguage() {
        return language;
    }

    public String getDisplayCountry() {
        return country;
    }

    public String getDisplayName() {
        return toString();
    }

    public String toLanguageTag() {
        return country.length() == 0 ? language : language + "-" + country;
    }

    public String toString() {
        return country.length() == 0 ? language : language + "_" + country;
    }

    public boolean equals(Object other) {
        if (!(other instanceof Locale)) {
            return false;
        }
        Locale l = (Locale) other;
        return language.equals(l.language) && country.equals(l.country);
    }

    public int hashCode() {
        return language.hashCode() * 31 + country.hashCode();
    }

    public static Locale getDefault() {
        return defaultLocale;
    }

    public static void setDefault(Locale locale) {
        if (locale != null) {
            defaultLocale = locale;
        }
    }

    public static Locale[] getAvailableLocales() {
        return new Locale[] { US, UK };
    }
}
