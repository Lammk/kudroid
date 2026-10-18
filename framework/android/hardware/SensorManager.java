package android.hardware;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public abstract class SensorManager {
    public static final int SENSOR_DELAY_FASTEST = 0, SENSOR_DELAY_GAME = 1, SENSOR_DELAY_UI = 2, SENSOR_DELAY_NORMAL = 3;
    public static final int SENSOR_STATUS_UNRELIABLE = 0, SENSOR_STATUS_ACCURACY_LOW = 1, SENSOR_STATUS_ACCURACY_MEDIUM = 2, SENSOR_STATUS_ACCURACY_HIGH = 3;
    public static final float GRAVITY_EARTH = 9.80665f;
    public static final int AXIS_X = 1, AXIS_Y = 2, AXIS_Z = 3;
    public static final int AXIS_MINUS_X = 129, AXIS_MINUS_Y = 130, AXIS_MINUS_Z = 131;
    private static final int[] TYPES = {1, 2, 4, 9, 10};
    private static final Sensor[] SENSORS = {
        new Sensor(1, "Accelerometer"), new Sensor(2, "Magnetic field"),
        new Sensor(4, "Gyroscope"), new Sensor(9, "Gravity"), new Sensor(10, "Linear acceleration")
    };
    private static native boolean nativeAvailable(int type);
    private static native void nativeConfigure(int type, int periodUs);
    private static native long nativeRead(int type, float[] values);

    private static final class Subscription {
        final SensorEventListener listener;
        final Sensor sensor;
        final Handler handler;
        int periodUs;
        long timestamp;
        volatile boolean active = true;
        volatile boolean pending;
        boolean accuracySent;
        Subscription(SensorEventListener l, Sensor s, Handler h, int period) {
            listener = l; sensor = s; handler = h; periodUs = period;
        }
    }
    private static final ArrayList<Subscription> SUBSCRIPTIONS = new ArrayList<Subscription>();
    private static Handler reader;
    private static boolean scheduled;
    private static final Runnable READ = new Runnable() {
        public void run() {
            synchronized (SUBSCRIPTIONS) {
                scheduled = false;
                int delayMs = 20;
                for (final Subscription subscription : SUBSCRIPTIONS) {
                    final SensorEvent event = new SensorEvent(3);
                    long timestamp = nativeRead(subscription.sensor.getType(), event.values);
                    delayMs = Math.min(delayMs, Math.max(5, subscription.periodUs / 1000));
                    if (subscription.pending || timestamp == 0 || timestamp <= subscription.timestamp ||
                            timestamp - subscription.timestamp < (long) subscription.periodUs * 1000) continue;
                    subscription.timestamp = timestamp;
                    event.sensor = subscription.sensor;
                    event.timestamp = timestamp;
                    event.accuracy = SENSOR_STATUS_ACCURACY_HIGH;
                    subscription.pending = true;
                    subscription.handler.post(new Runnable() {
                        public void run() {
                            try {
                                if (!subscription.active) return;
                                if (!subscription.accuracySent) {
                                    subscription.accuracySent = true;
                                    subscription.listener.onAccuracyChanged(subscription.sensor, event.accuracy);
                                }
                                if (subscription.active) subscription.listener.onSensorChanged(event);
                            } finally {
                                subscription.pending = false;
                            }
                        }
                    });
                }
                if (!SUBSCRIPTIONS.isEmpty()) {
                    scheduled = true;
                    reader.postDelayed(this, delayMs);
                }
            }
        }
    };

    public List<Sensor> getSensorList(int type) {
        ArrayList<Sensor> available = new ArrayList<Sensor>();
        for (Sensor sensor : SENSORS) if ((type == Sensor.TYPE_ALL || sensor.getType() == type) && nativeAvailable(sensor.getType())) available.add(sensor);
        return Collections.unmodifiableList(available);
    }
    public Sensor getDefaultSensor(int type) {
        for (Sensor sensor : SENSORS) if (sensor.getType() == type && nativeAvailable(type)) return sensor;
        return null;
    }
    public Sensor getDefaultSensor(int type, boolean wakeUp) { return wakeUp ? null : getDefaultSensor(type); }
    private static int period(int rate) {
        switch (rate) {
            case SENSOR_DELAY_FASTEST: return 5000;
            case SENSOR_DELAY_GAME: return 20000;
            case SENSOR_DELAY_UI: return 66667;
            case SENSOR_DELAY_NORMAL: return 200000;
            default: return Math.max(5000, rate);
        }
    }
    private static void configure(int type) {
        int fastest = Integer.MAX_VALUE;
        for (Subscription subscription : SUBSCRIPTIONS) if (subscription.sensor.getType() == type) fastest = Math.min(fastest, subscription.periodUs);
        nativeConfigure(type, fastest == Integer.MAX_VALUE ? 0 : fastest);
    }
    public boolean registerListener(SensorEventListener listener, Sensor sensor, int rate) { return registerListener(listener, sensor, rate, (Handler) null); }
    public boolean registerListener(SensorEventListener listener, Sensor sensor, int rate, int maxReportLatencyUs) { return registerListener(listener, sensor, rate, (Handler) null); }
    public boolean registerListener(SensorEventListener listener, Sensor sensor, int rate, int maxReportLatencyUs, Handler handler) { return registerListener(listener, sensor, rate, handler); }
    public boolean registerListener(SensorEventListener listener, Sensor sensor, int rate, Handler handler) {
        if (listener == null || sensor == null || rate < 0 || !nativeAvailable(sensor.getType())) return false;
        synchronized (SUBSCRIPTIONS) {
            for (Subscription subscription : SUBSCRIPTIONS) {
                if (subscription.listener == listener && subscription.sensor.getType() == sensor.getType()) {
                    subscription.periodUs = period(rate);
                    configure(sensor.getType());
                    return true;
                }
            }
            Handler target = handler == null ? new Handler(Looper.getMainLooper()) : handler;
            SUBSCRIPTIONS.add(new Subscription(listener, sensor, target, period(rate)));
            configure(sensor.getType());
            if (reader == null) {
                HandlerThread thread = new HandlerThread("SensorReader");
                thread.start();
                reader = new Handler(thread.getLooper());
            }
            if (!scheduled) { scheduled = true; reader.post(READ); }
            return true;
        }
    }
    public void unregisterListener(SensorEventListener listener) { unregisterListener(listener, null); }
    public void unregisterListener(SensorEventListener listener, Sensor sensor) {
        synchronized (SUBSCRIPTIONS) {
            for (int i = SUBSCRIPTIONS.size() - 1; i >= 0; --i) {
                Subscription subscription = SUBSCRIPTIONS.get(i);
                if (subscription.listener == listener && (sensor == null || subscription.sensor.getType() == sensor.getType())) {
                    subscription.active = false;
                    SUBSCRIPTIONS.remove(i);
                }
            }
            for (int type : TYPES) configure(type);
        }
    }

    public static boolean getRotationMatrix(float[] rotation, float[] inclination, float[] gravity, float[] magnetic) {
        if (gravity == null || magnetic == null || gravity.length < 3 || magnetic.length < 3) return false;
        double gx = gravity[0], gy = gravity[1], gz = gravity[2];
        double normG = Math.sqrt(gx * gx + gy * gy + gz * gz);
        if (normG < 0.981) return false;
        gx /= normG; gy /= normG; gz /= normG;
        double ex = magnetic[1] * gz - magnetic[2] * gy;
        double ey = magnetic[2] * gx - magnetic[0] * gz;
        double ez = magnetic[0] * gy - magnetic[1] * gx;
        double normE = Math.sqrt(ex * ex + ey * ey + ez * ez);
        if (normE < 0.1) return false;
        ex /= normE; ey /= normE; ez /= normE;
        double nx = gy * ez - gz * ey, ny = gz * ex - gx * ez, nz = gx * ey - gy * ex;
        if (rotation != null) setMatrix(rotation, ex, ey, ez, nx, ny, nz, gx, gy, gz);
        if (inclination != null) {
            double normM = Math.sqrt((double) magnetic[0] * magnetic[0] + (double) magnetic[1] * magnetic[1] + (double) magnetic[2] * magnetic[2]);
            if (normM == 0) return false;
            double cosine = (magnetic[0] * nx + magnetic[1] * ny + magnetic[2] * nz) / normM;
            double sine = (magnetic[0] * gx + magnetic[1] * gy + magnetic[2] * gz) / normM;
            setMatrix(inclination, 1, 0, 0, 0, cosine, sine, 0, -sine, cosine);
        }
        return true;
    }
    private static void setMatrix(float[] out, double a, double b, double c, double d, double e, double f, double g, double h, double i) {
        if (out.length != 9 && out.length != 16) throw new IllegalArgumentException("Matrix must have 9 or 16 elements");
        int stride = out.length == 9 ? 3 : 4;
        for (int n = 0; n < out.length; ++n) out[n] = 0;
        out[0] = (float) a; out[1] = (float) b; out[2] = (float) c;
        out[stride] = (float) d; out[stride + 1] = (float) e; out[stride + 2] = (float) f;
        out[stride * 2] = (float) g; out[stride * 2 + 1] = (float) h; out[stride * 2 + 2] = (float) i;
        if (out.length == 16) out[15] = 1;
    }
    public static float[] getOrientation(float[] rotation, float[] values) {
        if (values == null || values.length < 3 || (rotation.length != 9 && rotation.length != 16)) throw new IllegalArgumentException("Invalid orientation matrix");
        int stride = rotation.length == 9 ? 3 : 4;
        values[0] = (float) Math.atan2(rotation[1], rotation[stride + 1]);
        values[1] = (float) Math.asin(Math.max(-1, Math.min(1, -rotation[stride * 2 + 1])));
        values[2] = (float) Math.atan2(-rotation[stride * 2], rotation[stride * 2 + 2]);
        return values;
    }
}
