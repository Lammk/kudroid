package java.text;

/**
 * java.text.DateFormatSymbols — the names a date formatter substitutes.
 *
 * English only. Locale-specific names live in the ICU data KuDroid does not ship, and
 * an app that formats a date still needs *some* names; falling back to English keeps
 * output readable rather than empty. setters are honoured so an app can supply its own
 * localised names.
 */
public class DateFormatSymbols implements java.io.Serializable, Cloneable {

    private String localPatternChars = "GyMdkHmsSEDFwWahKzZ";

    private String[] ampms = { "AM", "PM" };
    private String[] eras = { "BC", "AD" };

    private String[] months = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December", "",
    };

    private String[] shortMonths = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", "",
    };

    // Index 0 is unused: Calendar.SUNDAY is 1, so the arrays are 1-based.
    private String[] weekdays = {
        "", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    };

    private String[] shortWeekdays = {
        "", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
    };

    private String[][] zoneStrings = new String[0][0];

    public DateFormatSymbols() {
    }

    public DateFormatSymbols(java.util.Locale locale) {
    }

    private static String[] copy(String[] src) {
        String[] out = new String[src.length];
        for (int i = 0; i < src.length; i++) out[i] = src[i];
        return out;
    }

    public String[] getAmPmStrings() {
        return copy(ampms);
    }

    public void setAmPmStrings(String[] data) {
        ampms = copy(data);
    }

    public String[] getEras() {
        return copy(eras);
    }

    public void setEras(String[] data) {
        eras = copy(data);
    }

    public String[] getMonths() {
        return copy(months);
    }

    public void setMonths(String[] data) {
        months = copy(data);
    }

    public String[] getShortMonths() {
        return copy(shortMonths);
    }

    public void setShortMonths(String[] data) {
        shortMonths = copy(data);
    }

    public String[] getWeekdays() {
        return copy(weekdays);
    }

    public void setWeekdays(String[] data) {
        weekdays = copy(data);
    }

    public String[] getShortWeekdays() {
        return copy(shortWeekdays);
    }

    public void setShortWeekdays(String[] data) {
        shortWeekdays = copy(data);
    }

    public String getLocalPatternChars() {
        return localPatternChars;
    }

    public void setLocalPatternChars(String data) {
        if (data == null) throw new NullPointerException();
        localPatternChars = data;
    }

    public String[][] getZoneStrings() {
        return zoneStrings;
    }

    public void setZoneStrings(String[][] data) {
        zoneStrings = data;
    }

    @Override
    public Object clone() {
        try {
            DateFormatSymbols copy = (DateFormatSymbols) super.clone();
            copy.ampms = copy(ampms);
            copy.eras = copy(eras);
            copy.months = copy(months);
            copy.shortMonths = copy(shortMonths);
            copy.weekdays = copy(weekdays);
            copy.shortWeekdays = copy(shortWeekdays);
            return copy;
        } catch (CloneNotSupportedException e) {
            return null;
        }
    }

    @Override
    public boolean equals(Object object) {
        if (this == object) return true;
        if (!(object instanceof DateFormatSymbols)) return false;
        DateFormatSymbols other = (DateFormatSymbols) object;
        return localPatternChars.equals(other.localPatternChars)
                && java.util.Arrays.equals(ampms, other.ampms)
                && java.util.Arrays.equals(eras, other.eras)
                && java.util.Arrays.equals(months, other.months)
                && java.util.Arrays.equals(shortMonths, other.shortMonths)
                && java.util.Arrays.equals(weekdays, other.weekdays)
                && java.util.Arrays.equals(shortWeekdays, other.shortWeekdays);
    }

    @Override
    public int hashCode() {
        return localPatternChars.hashCode();
    }
}
