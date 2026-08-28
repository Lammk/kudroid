package java.util;

/**
 * java.util.TimeZone — minimal implementation backed by the host's UTC offset.
 *
 * Real TimeZone reads the zoneinfo database and applies DST rules per instant.
 * KuDroid has no zoneinfo, so this exposes a fixed offset supplied by the runtime.
 * That is accurate for formatting timestamps and for code that only asks for
 * getDefault()/getRawOffset(), which is what apps do at startup — and it is far
 * better than the class being absent, which made TimeZone.getDefault() return null
 * and blow up as an unexplained NullPointerException in the caller.
 */
public class TimeZone {
    /** Milliseconds east of UTC, filled in by the runtime. */
    private static native int native_getDefaultRawOffset();

    /** Host zone abbreviation, e.g. "UTC" or "ICT". */
    private static native String native_getDefaultId();

    private static TimeZone sDefault;

    private final String mId;
    private final int mRawOffset;

    protected TimeZone() {
        mId = "UTC";
        mRawOffset = 0;
    }

    private TimeZone(String id, int rawOffset) {
        mId = id;
        mRawOffset = rawOffset;
    }

    public static synchronized TimeZone getDefault() {
        if (sDefault == null) {
            String id = "UTC";
            int offset = 0;
            try {
                String nativeId = native_getDefaultId();
                if (nativeId != null && nativeId.length() > 0) id = nativeId;
                offset = native_getDefaultRawOffset();
            } catch (Throwable ignored) {}
            sDefault = new TimeZone(id, offset);
        }
        return sDefault;
    }

    public static synchronized void setDefault(TimeZone zone) {
        sDefault = zone;
    }

    /**
     * Only "GMT"/"UTC" and fixed "GMT+hh:mm" forms are understood. Any other id
     * yields UTC, matching the JDK contract that an unknown id falls back to GMT
     * rather than throwing.
     */
    public static TimeZone getTimeZone(String id) {
        if (id == null) return new TimeZone("UTC", 0);
        if (id.equals("UTC") || id.equals("GMT") || id.equals("GMT+0") || id.equals("GMT-0")) {
            return new TimeZone("UTC", 0);
        }
        if (id.startsWith("GMT+") || id.startsWith("GMT-")) {
            final int sign = id.charAt(3) == '-' ? -1 : 1;
            final String rest = id.substring(4);
            int hours = 0;
            int minutes = 0;
            final int colon = rest.indexOf(':');
            try {
                if (colon >= 0) {
                    hours = Integer.parseInt(rest.substring(0, colon));
                    minutes = Integer.parseInt(rest.substring(colon + 1));
                } else {
                    hours = Integer.parseInt(rest);
                }
            } catch (Throwable ignored) {
                return new TimeZone("UTC", 0);
            }
            return new TimeZone(id, sign * (hours * 3600000 + minutes * 60000));
        }
        // Unknown named zone: keep the id for display but no offset information.
        return new TimeZone(id, 0);
    }

    public static String[] getAvailableIDs() {
        return new String[] { "UTC", "GMT" };
    }

    public String getID() {
        return mId;
    }

    public int getRawOffset() {
        return mRawOffset;
    }

    public void setRawOffset(int offsetMillis) {
    }

    /** No DST rules available, so the offset is the raw offset at every instant. */
    public int getOffset(long date) {
        return mRawOffset;
    }

    public boolean useDaylightTime() {
        return false;
    }

    public boolean inDaylightTime(Date date) {
        return false;
    }

    public int getDSTSavings() {
        return 0;
    }

    public String getDisplayName() {
        return mId;
    }

    public String getDisplayName(boolean daylight, int style) {
        return mId;
    }

    public boolean hasSameRules(TimeZone other) {
        return other != null && other.mRawOffset == mRawOffset;
    }

    @Override
    public String toString() {
        return "TimeZone[id=" + mId + ",offset=" + mRawOffset + "]";
    }
}
