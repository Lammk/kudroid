package android.hardware;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * emulate android.hardware.sensormanager integrating real rotation sensor from iOS CoreMotion.
 */
public class SensorManager {
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

    public static final int SENSOR_DELAY_NORMAL = 3;
    public static final int SENSOR_DELAY_UI = 2;
    public static final int SENSOR_DELAY_GAME = 1;
    public static final int SENSOR_DELAY_FASTEST = 0;

    public static final int SENSOR_STATUS_ACCURACY_HIGH = 3;

    private static final Sensor sAccel = new Sensor(TYPE_ACCELEROMETER, "iOS Accelerometer", "Apple", 0.001f, 10000);
    private static final Sensor sGyro = new Sensor(TYPE_GYROSCOPE, "iOS Gyroscope", "Apple", 0.001f, 10000);
    private static final Sensor sOrient = new Sensor(TYPE_ORIENTATION, "iOS Orientation Sensor", "Apple", 0.1f, 10000);

    private static final List<ListenerRegistration> sRegistrations = new CopyOnWriteArrayList<ListenerRegistration>();

    private static class ListenerRegistration {
        final SensorEventListener listener;
        final Sensor sensor;
        ListenerRegistration(SensorEventListener l, Sensor s) {
            this.listener = l;
            this.sensor = s;
        }
    }

    public SensorManager() {
    }

    public List<Sensor> getSensorList(int type) {
        List<Sensor> list = new ArrayList<Sensor>();
        if (type == TYPE_ACCELEROMETER || type == -1) list.add(sAccel);
        if (type == TYPE_GYROSCOPE || type == -1) list.add(sGyro);
        if (type == TYPE_ORIENTATION || type == -1) list.add(sOrient);
        return list;
    }

    public Sensor getDefaultSensor(int type) {
        if (type == TYPE_ACCELEROMETER) return sAccel;
        if (type == TYPE_GYROSCOPE) return sGyro;
        if (type == TYPE_ORIENTATION) return sOrient;
        return null;
    }

    public boolean registerListener(SensorEventListener listener, Sensor sensor, int samplingPeriodUs) {
        return registerListener(listener, sensor, samplingPeriodUs, null);
    }

    public boolean registerListener(SensorEventListener listener, Sensor sensor, int samplingPeriodUs,
                                    android.os.Handler handler) {
        if (listener == null || sensor == null) return false;
        unregisterListener(listener, sensor);
        sRegistrations.add(new ListenerRegistration(listener, sensor));
        return true;
    }

    public void unregisterListener(SensorEventListener listener) {
        if (listener == null) return;
        List<ListenerRegistration> toRemove = new ArrayList<ListenerRegistration>();
        for (ListenerRegistration reg : sRegistrations) {
            if (reg.listener == listener) toRemove.add(reg);
        }
        sRegistrations.removeAll(toRemove);
    }

    public void unregisterListener(SensorEventListener listener, Sensor sensor) {
        if (listener == null) return;
        List<ListenerRegistration> toRemove = new ArrayList<ListenerRegistration>();
        for (ListenerRegistration reg : sRegistrations) {
            if (reg.listener == listener && (sensor == null || reg.sensor == sensor || reg.sensor.getType() == sensor.getType())) {
                toRemove.add(reg);
            }
        }
        sRegistrations.removeAll(toRemove);
    }

    /**
     * Called by C++ Native Bridge when iPhone has a sensor event from CoreMotion.
     */
    public static void onSensorChanged_from_native(int sensorType, float x, float y, float z) {
        Sensor s = null;
        if (sensorType == TYPE_ACCELEROMETER) s = sAccel;
        else if (sensorType == TYPE_GYROSCOPE) s = sGyro;
        else if (sensorType == TYPE_ORIENTATION) s = sOrient;
        if (s == null) return;

        SensorEvent event = new SensorEvent(s, SENSOR_STATUS_ACCURACY_HIGH, System.currentTimeMillis() * 1000000L, new float[] { x, y, z });
        for (ListenerRegistration reg : sRegistrations) {
            if (reg.sensor.getType() == sensorType) {
                try {
                    reg.listener.onSensorChanged(event);
                } catch (Throwable t) {
                    t.printStackTrace();
                }
            }
        }
    }

    /**
     * listener for sensor events.
     */
    public interface SensorEventListener {
        void onSensorChanged(SensorEvent event);
        void onAccuracyChanged(Sensor sensor, int accuracy);
    }
}