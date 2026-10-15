package java.util.zip;

import java.lang.Cloneable;

public class ZipEntry implements Cloneable {
    String name;
    long time = -1;
    long crc = -1;
    long size = -1;
    long csize = -1;
    int method = -1;
    byte[] extra;
    String comment;

    public static final int STORED = 0;
    public static final int DEFLATED = 8;

    public ZipEntry(String name) {
        if (name == null) throw new NullPointerException();
        if (name.length() > 0xFFFF) throw new IllegalArgumentException("entry name too long");
        this.name = name;
    }

    public ZipEntry(ZipEntry e) {
        name = e.name;
        time = e.time;
        crc = e.crc;
        size = e.size;
        csize = e.csize;
        method = e.method;
        extra = e.extra;
        comment = e.comment;
    }

    public String getName() { return name; }
    public void setTime(long time) { this.time = time; }
    public long getTime() { return time; }
    public void setSize(long size) { this.size = size; }
    public long getSize() { return size; }
    public long getCompressedSize() { return csize; }
    public void setCompressedSize(long csize) { this.csize = csize; }
    public void setCrc(long crc) { this.crc = crc; }
    public long getCrc() { return crc; }
    public void setMethod(int method) { this.method = method; }
    public int getMethod() { return method; }
    public void setExtra(byte[] extra) { this.extra = extra; }
    public byte[] getExtra() { return extra; }
    public void setComment(String comment) { this.comment = comment; }
    public String getComment() { return comment; }
    public boolean isDirectory() { return name.endsWith("/"); }
    public String toString() { return getName(); }
    public int hashCode() { return name.hashCode(); }
    public Object clone() {
        try {
            return super.clone();
        } catch (CloneNotSupportedException e) {
            throw new InternalError();
        }
    }
}
