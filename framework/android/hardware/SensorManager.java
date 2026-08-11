package android.hardware;

/**
 * mô phỏng android.hardware.sensormanager.
 *
 * không quan trọng đối với khởi động/kết xuất ứng dụng. trả về 0 cảm biến để các ứng dụng
 * không gặp sự cố khi chúng truy vấn các cảm biến.
 */
public class SensorManager {
    /** loại cảm biến: gia tốc kế. */
    public static final int TYPE_ACCELEROMETER = 1;
    /** loại cảm biến: từ trường. */
    public static final int TYPE_MAGNETIC_FIELD = 2;
    /** loại cảm biến: hướng. */
    public static final int TYPE_ORIENTATION = 3;
    /** loại cảm biến: con quay hồi chuyển. */
    public static final int TYPE_GYROSCOPE = 4;
    /** loại cảm biến: ánh sáng. */
    public static final int TYPE_LIGHT = 5;
    /** loại cảm biến: áp suất. */
    public static final int TYPE_PRESSURE = 6;
    /** loại cảm biến: tiệm cận. */
    public static final int TYPE_PROXIMITY = 8;
    /** loại cảm biến: trọng lực. */
    public static final int TYPE_GRAVITY = 9;
    /** loại cảm biến: gia tốc tuyến tính. */
    public static final int TYPE_LINEAR_ACCELERATION = 10;
    /** loại cảm biến: véc tơ quay. */
    public static final int TYPE_ROTATION_VECTOR = 11;

    /** độ trễ của cảm biến: bình thường. */
    public static final int SENSOR_DELAY_NORMAL = 3;
    /** độ trễ của cảm biến: ui. */
    public static final int SENSOR_DELAY_UI = 2;
    /** độ trễ của cảm biến: trò chơi. */
    public static final int SENSOR_DELAY_GAME = 1;
    /** độ trễ của cảm biến: nhanh nhất. */
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
     * trình nghe cho các sự kiện cảm biến.
     */
    public interface SensorEventListener {
        void onSensorChanged(SensorEvent event);
        void onAccuracyChanged(Sensor sensor, int accuracy);
    }
}