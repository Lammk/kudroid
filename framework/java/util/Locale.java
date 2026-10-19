package java.util;

/**
 * A language/country/variant triple. Only the data needed to identify a locale is kept;
 * display names are the codes themselves.
 */
public final class Locale {

    public static final Locale ENGLISH = new Locale("en", "");
    public static final Locale FRENCH = new Locale("fr", "");
    public static final Locale GERMAN = new Locale("de", "");
    public static final Locale ITALIAN = new Locale("it", "");
    public static final Locale JAPANESE = new Locale("ja", "");
    public static final Locale KOREAN = new Locale("ko", "");
    public static final Locale CHINESE = new Locale("zh", "");
    public static final Locale SIMPLIFIED_CHINESE = new Locale("zh", "CN");
    public static final Locale TRADITIONAL_CHINESE = new Locale("zh", "TW");
    public static final Locale FRANCE = new Locale("fr", "FR");
    public static final Locale GERMANY = new Locale("de", "DE");
    public static final Locale ITALY = new Locale("it", "IT");
    public static final Locale JAPAN = new Locale("ja", "JP");
    public static final Locale KOREA = new Locale("ko", "KR");
    public static final Locale UK = new Locale("en", "GB");
    public static final Locale US = new Locale("en", "US");
    public static final Locale CANADA = new Locale("en", "CA");
    public static final Locale CANADA_FRENCH = new Locale("fr", "CA");
    public static final Locale CHINA = SIMPLIFIED_CHINESE;
    public static final Locale PRC = SIMPLIFIED_CHINESE;
    public static final Locale TAIWAN = TRADITIONAL_CHINESE;
    public static final Locale ROOT = new Locale("", "");

    /** Categories of default locale: one for user-visible text, one for formatting. */
    public enum Category {
        DISPLAY,
        FORMAT
    }

    private static Locale defaultLocale = US;
    private static Locale defaultDisplayLocale;
    private static Locale defaultFormatLocale;

    private final String language;
    private final String country;
    private final String variant;
    private final String script;

    public Locale(String language) {
        this(language, "", "");
    }

    public Locale(String language, String country) {
        this(language, country, "");
    }

    public Locale(String language, String country, String variant) {
        this(language, "", country, variant);
    }

    private Locale(String language, String script, String country, String variant) {
        this.language = language == null ? "" : normalizeLanguage(language);
        this.script = script == null ? "" : script;
        this.country = country == null ? "" : country.toUpperCase();
        this.variant = variant == null ? "" : variant;
    }

    // Legacy ISO codes map to their current form, as the platform does.
    private static String normalizeLanguage(String language) {
        final String lower = language.toLowerCase();
        switch (lower) {
            case "iw": return "he";
            case "in": return "id";
            case "ji": return "yi";
            default: return lower;
        }
    }

    public String getLanguage() {
        return language;
    }

    public String getCountry() {
        return country;
    }

    public String getVariant() {
        return variant;
    }

    public String getScript() {
        return script;
    }

    public String getDisplayLanguage() {
        return language;
    }

    public String getDisplayLanguage(Locale inLocale) {
        return language;
    }

    public String getDisplayCountry() {
        return country;
    }

    public String getDisplayCountry(Locale inLocale) {
        return country;
    }

    public String getDisplayName() {
        return toString();
    }

    public String getDisplayName(Locale inLocale) {
        return toString();
    }

    public String toLanguageTag() {
        if (language.length() == 0) {
            return "und";
        }
        final StringBuilder sb = new StringBuilder(language);
        if (script.length() != 0) {
            sb.append('-').append(script);
        }
        if (country.length() != 0) {
            sb.append('-').append(country);
        }
        if (variant.length() != 0) {
            sb.append('-').append(variant.replace('_', '-'));
        }
        return sb.toString();
    }

    /** Parses a BCP 47 tag of the form language[-script][-region][-variant]. */
    public static Locale forLanguageTag(String languageTag) {
        if (languageTag == null) {
            throw new NullPointerException("languageTag == null");
        }
        final String[] parts = languageTag.split("[-_]");
        if (parts.length == 0 || parts[0].equals("und") || parts[0].length() == 0) {
            return ROOT;
        }
        final String lang = parts[0];
        String script = "";
        String region = "";
        final StringBuilder variant = new StringBuilder();
        int i = 1;
        if (i < parts.length && parts[i].length() == 4 && Character.isLetter(parts[i].charAt(0))) {
            script = Character.toUpperCase(parts[i].charAt(0)) + parts[i].substring(1).toLowerCase();
            i++;
        }
        if (i < parts.length && (parts[i].length() == 2 || parts[i].length() == 3)) {
            region = parts[i];
            i++;
        }
        for (; i < parts.length; i++) {
            if (parts[i].length() == 1) {
                break;
            }
            if (variant.length() > 0) {
                variant.append('_');
            }
            variant.append(parts[i]);
        }
        return new Locale(lang, script, region, variant.toString());
    }

    public String toString() {
        final StringBuilder sb = new StringBuilder(language);
        if (country.length() != 0 || variant.length() != 0 || script.length() != 0) {
            sb.append('_').append(country);
        }
        if (variant.length() != 0) {
            sb.append('_').append(variant);
        }
        if (script.length() != 0) {
            sb.append("_#").append(script);
        }
        return sb.toString();
    }

    public boolean equals(Object other) {
        if (this == other) {
            return true;
        }
        if (!(other instanceof Locale)) {
            return false;
        }
        Locale l = (Locale) other;
        return language.equals(l.language) && country.equals(l.country)
                && variant.equals(l.variant) && script.equals(l.script);
    }

    public int hashCode() {
        int h = language.hashCode();
        h = h * 31 + country.hashCode();
        h = h * 31 + variant.hashCode();
        return h * 31 + script.hashCode();
    }

    public static Locale getDefault() {
        return defaultLocale;
    }

    public static Locale getDefault(Category category) {
        switch (category) {
            case DISPLAY:
                return defaultDisplayLocale != null ? defaultDisplayLocale : defaultLocale;
            default:
                return defaultFormatLocale != null ? defaultFormatLocale : defaultLocale;
        }
    }

    public static synchronized void setDefault(Locale locale) {
        if (locale != null) {
            defaultLocale = locale;
            defaultDisplayLocale = null;
            defaultFormatLocale = null;
        }
    }

    public static synchronized void setDefault(Category category, Locale locale) {
        if (category == null) {
            throw new NullPointerException("category == null");
        }
        if (locale == null) {
            throw new NullPointerException("locale == null");
        }
        if (category == Category.DISPLAY) {
            defaultDisplayLocale = locale;
        } else {
            defaultFormatLocale = locale;
        }
    }

    public static Locale[] getAvailableLocales() {
        return new Locale[] { US, UK };
    }
}
