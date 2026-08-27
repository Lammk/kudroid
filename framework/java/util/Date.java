package java.util;

public class Date {

    private long time;

    public Date() {
        this.time = System.currentTimeMillis();
    }

    public Date(long time) {
        this.time = time;
    }

    public long getTime() {
        return time;
    }

    public void setTime(long time) {
        this.time = time;
    }

    public boolean before(Date other) {
        return time < other.time;
    }

    public boolean after(Date other) {
        return time > other.time;
    }

    public boolean equals(Object other) {
        return (other instanceof Date) && ((Date) other).time == time;
    }

    public int hashCode() {
        return (int) (time ^ (time >>> 32));
    }

    public String toString() {
        return "Date(" + time + ")";
    }
}
