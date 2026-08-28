package android.graphics;

import android.os.Parcel;
import android.os.Parcelable;

public class PointF implements Parcelable {
    public float x;
    public float y;

    public PointF() {}
    public PointF(float x, float y) { this.x = x; this.y = y; }
    public PointF(Point p) { this.x = p.x; this.y = p.y; }
    public void set(float x, float y) { this.x = x; this.y = y; }
    public final void offset(float dx, float dy) { x += dx; y += dy; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel out, int flags) { out.writeFloat(x); out.writeFloat(y); }
}
