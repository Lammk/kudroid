package android.hardware;

/**
 * emulate android.hardware.sensor.
 *
 * represents a sensor. for kudroid minimal framework, here is an emulation.
 */
public class Sensor {
    /** reporting mode: continuous. */
    public static final int REPORTING_MODE_CONTINUOUS = 0;
    /** report mode: on change. */
    public static final int REPORTING_MODE_ON_CHANGE = 1;
    /** reporting mode: once. */
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