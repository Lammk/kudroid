package android.hardware;

/**
 * Stub android.hardware.SensorManager.
 *
 * Non-critical for app startup/rendering. Returns 0 sensors so apps don't
 * crash when they query sensors.
 */
public class SensorManager {
    /** Sensor type: accelerometer. */
    public static final int TYPE_ACCELEROMETER = 1;
    /** Sensor type: magnetic field. */
    public static final int TYPE_MAGNETIC_FIELD = 2;
    /** Sensor type: orientation. */
    public static final int TYPE_ORIENTATION = 3;
    /** Sensor type: gyroscope. */
    public static final int TYPE_GYROSCOPE = 4;
    /** Sensor type: light. */
    public static final int TYPE_LIGHT = 5;
    /** Sensor type: pressure. */
    public static final int TYPE_PRESSURE = 6;
    /** Sensor type: proximity. */
    public static final int TYPE_PROXIMITY = 8;
    /** Sensor type: gravity. */
    public static final int TYPE_GRAVITY = 9;
    /** Sensor type: linear acceleration. */
    public static final int TYPE_LINEAR_ACCELERATION = 10;
    /** Sensor type: rotation vector. */
    public static final int TYPE_ROTATION_VECTOR = 11;

    /** Sensor delay: normal. */
    public static final int SENSOR_DELAY_NORMAL = 3;
    /** Sensor delay: UI. */
    public static final int SENSOR_DELAY_UI = 2;
    /** Sensor delay: game. */
    public static final int SENSOR_DELAY_GAME = 1;
    /** Sensor delay: fastest. */
    public static final int SENSOR_DELAY_FASTEST = 0;

    public SensorManager() {
    }

    public java.util.List<Sensor> getSensorList(int type) {
        return new java.util.ArrayList<Sensor>();
    }

    public Sensor getDefaultSensor(int type) {
        return null;
    }

    public boolean registerListener(SensorEventListener listener, Sensor sensor, int samplingPeriodUs) {
        return false;
    }

    public boolean registerListener(SensorEventListener listener, Sensor sensor, int samplingPeriodUs,
                                    android.os.Handler handler) {
        return false;
    }

    public void unregisterListener(SensorEventListener listener) {
    }

    public void unregisterListener(SensorEventListener listener, Sensor sensor) {
    }

    /**
     * Listener for sensor events.
     */
    public interface SensorEventListener {
        void onSensorChanged(SensorEvent event);
        void onAccuracyChanged(Sensor sensor, int accuracy);
    }
}