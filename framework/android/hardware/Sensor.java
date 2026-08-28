package android.hardware;

public final class Sensor {
    public static final int TYPE_ACCELEROMETER = 1;
    public static final int TYPE_MAGNETIC_FIELD = 2;
    public static final int TYPE_ORIENTATION = 3;
    public static final int TYPE_GYROSCOPE = 4;
    public static final int TYPE_LIGHT = 5;
    public static final int TYPE_PRESSURE = 6;
    public static final int TYPE_PROXIMITY = 8;
    public static final int TYPE_GRAVITY = 9;
    public static final int TYPE_LINEAR_ACCELERATION = 10;
    public static final int TYPE_ROTATION_VECTOR = 11;

    private int mType;
    private String mName;
    Sensor(int type, String name) { mType = type; mName = name; }
    public String getName() { return mName; }
    public int getType() { return mType; }
    public float getMaximumRange() { return 100.0f; }
    public float getResolution() { return 0.01f; }
    public float getPower() { return 0.1f; }
    public int getMinDelay() { return 10000; }
}
