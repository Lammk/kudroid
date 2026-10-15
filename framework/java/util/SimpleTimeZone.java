package java.util;

public class SimpleTimeZone extends TimeZone {
    private static final long serialVersionUID = -403250971215465050L;
    private int rawOffset;
    private String zoneId;

    public SimpleTimeZone(int rawOffset, String ID) {
        super();
        this.rawOffset = rawOffset;
        this.zoneId = ID;
    }
    public String getID() { return zoneId != null ? zoneId : super.getID(); }
    public void setID(String ID) { this.zoneId = ID; }
    public int getOffset(int era, int year, int month, int day, int dayOfWeek, int milliseconds) { return rawOffset; }
    public int getRawOffset() { return rawOffset; }
    public void setRawOffset(int offsetMillis) { this.rawOffset = offsetMillis; }
    public boolean useDaylightTime() { return false; }
    public boolean inDaylightTime(Date date) { return false; }
}
