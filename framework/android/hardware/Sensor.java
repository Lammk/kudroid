package android.hardware;

/**
 * mô phỏng android.hardware.sensor.
 *
 * đại diện cho một cảm biến. đối với khuôn khổ tối thiểu của kudroid, đây là một mô phỏng.
 */
public class Sensor {
    /** chế độ báo cáo: liên tục. */
    public static final int REPORTING_MODE_CONTINUOUS = 0;
    /** chế độ báo cáo: khi thay đổi. */
    public static final int REPORTING_MODE_ON_CHANGE = 1;
    /** chế độ báo cáo: một lần. */
    public static final int REPORTING_MODE_ONE_SHOT = 2;

    private final int mType;
    private final String mName;
    private final String mVendor;
    private final float mResolution;
    private final int mMinDelay;

    public Sensor(int type, String name, String vendor, float resolution, int minDelay) {
        mType = type;
        mName = name;
        mVendor = vendor;
        mResolution = resolution;
        mMinDelay = minDelay;
    }

    public int getType() {
        return mType;
    }

    public String getName() {
        return mName;
    }

    public String getVendor() {
        return mVendor;
    }

    public float getResolution() {
        return mResolution;
    }

    public int getMinDelay() {
        return mMinDelay;
    }

    public float getMaximumRange() {
        return 0.0f;
    }

    public int getVersion() {
        return 1;
    }
}