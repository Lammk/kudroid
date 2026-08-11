package android.location;

/**
 * mô phỏng android.location.location.
 *
 * đại diện cho một vị trí địa lý. đối với khuôn khổ tối thiểu của kudroid, đây là
 * một mô phỏng với các giá trị mặc định.
 */
public class Location {
    private double mLatitude = 0.0;
    private double mLongitude = 0.0;
    private double mAltitude = 0.0;
    private float mAccuracy = 0.0f;
    private long mTime = 0;

    public Location(String provider) {
    }

    public Location(Location l) {
        if (l != null) {
            mLatitude = l.mLatitude;
            mLongitude = l.mLongitude;
            mAltitude = l.mAltitude;
            mAccuracy = l.mAccuracy;
            mTime = l.mTime;
        }
    }

    public double getLatitude() {
        return mLatitude;
    }

    public void setLatitude(double latitude) {
        mLatitude = latitude;
    }

    public double getLongitude() {
        return mLongitude;
    }

    public void setLongitude(double longitude) {
        mLongitude = longitude;
    }

    public double getAltitude() {
        return mAltitude;
    }

    public void setAltitude(double altitude) {
        mAltitude = altitude;
    }

    public float getAccuracy() {
        return mAccuracy;
    }

    public void setAccuracy(float accuracy) {
        mAccuracy = accuracy;
    }

    public long getTime() {
        return mTime;
    }

    public void setTime(long time) {
        mTime = time;
    }

    public boolean hasAccuracy() {
        return mAccuracy > 0;
    }

    public float distanceTo(Location dest) {
        return 0.0f;
    }
}