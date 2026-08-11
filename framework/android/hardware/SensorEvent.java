package android.hardware;

/**
 * mô phỏng android.hardware.sensorevent.
 *
 * đại diện cho một sự kiện cảm biến. đối với khuôn khổ tối thiểu của kudroid, đây là một mô phỏng.
 */
public class SensorEvent {
    /** cảm biến đã tạo ra sự kiện này. */
    public final Sensor sensor;
    /** độ dài của mảng các giá trị. */
    public final int accuracy;
    /** dấu thời gian của sự kiện. */
    public final long timestamp;
    /** các giá trị của cảm biến. */
    public final float[] values;

    public SensorEvent(Sensor sensor, int accuracy, long timestamp, float[] values) {
        this.sensor = sensor;
        this.accuracy = accuracy;
        this.timestamp = timestamp;
        this.values = values;
    }
}