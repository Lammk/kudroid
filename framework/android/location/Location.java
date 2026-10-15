package android.location;

import android.os.Parcel;
import android.os.Parcelable;

public class Location implements Parcelable {
    private String mProvider = "gps";
    private double mLatitude = 0.0;
    private double mLongitude = 0.0;
    private double mAltitude = 0.0;
    private float mSpeed = 0.0f;
    private float mBearing = 0.0f;
    private float mAccuracy = 5.0f;
    private long mTime = System.currentTimeMillis();

    public Location(String provider) { this.mProvider = provider; }
    public Location(Location l) {
        if (l != null) {
            this.mProvider = l.mProvider;
            this.mLatitude = l.mLatitude;
            this.mLongitude = l.mLongitude;
            this.mAltitude = l.mAltitude;
            this.mSpeed = l.mSpeed;
            this.mBearing = l.mBearing;
            this.mAccuracy = l.mAccuracy;
            this.mTime = l.mTime;
        }
    }
    public double getLatitude() { return mLatitude; }
    public void setLatitude(double latitude) { mLatitude = latitude; }
    public double getLongitude() { return mLongitude; }
    public void setLongitude(double longitude) { mLongitude = longitude; }
    public double getAltitude() { return mAltitude; }
    public void setAltitude(double altitude) { mAltitude = altitude; }
    public float getSpeed() { return mSpeed; }
    public void setSpeed(float speed) { mSpeed = speed; }
    public float getBearing() { return mBearing; }
    public void setBearing(float bearing) { mBearing = bearing; }
    public float getAccuracy() { return mAccuracy; }
    public void setAccuracy(float accuracy) { mAccuracy = accuracy; }
    public long getTime() { return mTime; }
    public void setTime(long time) { mTime = time; }
    public String getProvider() { return mProvider; }
    public void setProvider(String provider) { mProvider = provider; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
