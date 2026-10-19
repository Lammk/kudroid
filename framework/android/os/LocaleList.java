package android.os;

import java.io.Serializable;
import java.util.ArrayList;
import java.util.Locale;

/** An immutable, ordered list of locales, most preferred first. */
public final class LocaleList implements Parcelable, Serializable {
    private static final long serialVersionUID = 1L;
    private static final LocaleList sEmptyList = new LocaleList();
    private static LocaleList sDefaultLocaleList;
    private static LocaleList sLastDefaultLocaleList;
    private static Locale sLastExplicitlySetLocale;
    private static LocaleList sDefaultAdjustedLocaleList;

    private final Locale[] mList;
    private final String mStringRepresentation;

    public LocaleList(Locale... list) {
        final ArrayList<Locale> unique = new ArrayList<Locale>();
        if (list != null) {
            for (Locale l : list) {
                if (l == null) {
                    throw new NullPointerException("list[] must not contain null");
                }
                if (!unique.contains(l)) {
                    unique.add(l);
                }
            }
        }
        mList = unique.toArray(new Locale[unique.size()]);
        final StringBuilder sb = new StringBuilder();
        for (int i = 0; i < mList.length; i++) {
            if (i > 0) {
                sb.append(',');
            }
            sb.append(mList[i].toLanguageTag());
        }
        mStringRepresentation = sb.toString();
    }

    public Locale get(int index) {
        return (index >= 0 && index < mList.length) ? mList[index] : null;
    }

    public boolean isEmpty() {
        return mList.length == 0;
    }

    public int size() {
        return mList.length;
    }

    public int indexOf(Locale locale) {
        for (int i = 0; i < mList.length; i++) {
            if (mList[i].equals(locale)) {
                return i;
            }
        }
        return -1;
    }

    @Override
    public boolean equals(Object other) {
        if (other == this) {
            return true;
        }
        if (!(other instanceof LocaleList)) {
            return false;
        }
        return java.util.Arrays.equals(mList, ((LocaleList) other).mList);
    }

    @Override
    public int hashCode() {
        int result = 1;
        for (Locale l : mList) {
            result = 31 * result + l.hashCode();
        }
        return result;
    }

    @Override
    public String toString() {
        final StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < mList.length; i++) {
            if (i > 0) {
                sb.append(',');
            }
            sb.append(mList[i]);
        }
        return sb.append(']').toString();
    }

    /** The tags of all locales joined with commas, e.g. "en-US,vi-VN". */
    public String toLanguageTags() {
        return mStringRepresentation;
    }

    /** Parses a comma separated list of language tags. */
    public static LocaleList forLanguageTags(String list) {
        if (list == null || list.isEmpty()) {
            return getEmptyLocaleList();
        }
        final String[] tags = list.split(",");
        final ArrayList<Locale> locales = new ArrayList<Locale>();
        for (String tag : tags) {
            final Locale l = Locale.forLanguageTag(tag.trim());
            if (!l.getLanguage().isEmpty()) {
                locales.add(l);
            }
        }
        return new LocaleList(locales.toArray(new Locale[locales.size()]));
    }

    /** The first locale whose language matches one of the supported tags, or null. */
    public Locale getFirstMatch(String[] supportedLocales) {
        for (Locale l : mList) {
            for (String tag : supportedLocales) {
                final Locale s = Locale.forLanguageTag(tag);
                if (s.getLanguage().equals(l.getLanguage())
                        && (s.getCountry().isEmpty() || s.getCountry().equals(l.getCountry()))) {
                    return l;
                }
            }
        }
        return null;
    }

    public static LocaleList getEmptyLocaleList() {
        return sEmptyList;
    }

    public static synchronized LocaleList getDefault() {
        final Locale defaultLocale = Locale.getDefault();
        if (!defaultLocale.equals(sLastExplicitlySetLocale)) {
            sLastExplicitlySetLocale = defaultLocale;
            if (sDefaultLocaleList != null && sDefaultLocaleList.indexOf(defaultLocale) == 0) {
                return sDefaultLocaleList;
            }
            sDefaultLocaleList = new LocaleList(defaultLocale);
            sLastDefaultLocaleList = sDefaultLocaleList;
        }
        return sDefaultLocaleList;
    }

    public static synchronized LocaleList getAdjustedDefault() {
        return getDefault();
    }

    public static synchronized void setDefault(LocaleList locales) {
        if (locales == null) {
            throw new NullPointerException("locales is null");
        }
        if (locales.isEmpty()) {
            throw new IllegalArgumentException("locales is empty");
        }
        sLastDefaultLocaleList = locales;
        sDefaultLocaleList = locales;
        sLastExplicitlySetLocale = locales.get(0);
        Locale.setDefault(sLastExplicitlySetLocale);
    }

    public int describeContents() {
        return 0;
    }

    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(mStringRepresentation);
    }

    public static final Parcelable.Creator<LocaleList> CREATOR =
            new Parcelable.Creator<LocaleList>() {
                public LocaleList createFromParcel(Parcel source) {
                    return LocaleList.forLanguageTags(source.readString());
                }

                public LocaleList[] newArray(int size) {
                    return new LocaleList[size];
                }
            };
}
