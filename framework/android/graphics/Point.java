package android.graphics;

import android.os.Parcel;
import android.os.Parcelable;

public class Point implements Parcelable {
    public int x;
    public int y;

    public Point() {}
    public Point(int x, int y) { this.x = x; this.y = y; }
    public Point(Point src) { this.x = src.x; this.y = src.y; }
    public void set(int x, int y) { this.x = x; this.y = y; }
    public final void offset(int dx, int dy) { x += dx; y += dy; }
    public final boolean equals(int x, int y) { return this.x == x && this.y == y; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel out, int flags) { out.writeInt(x); out.writeInt(y); }
}
