package android.hardware;

/**
 * emulate android.hardware.sensorevent.
 *
 * represents a sensor event. for kudroid minimal framework, here is an emulation.
 */
public class SensorEvent {
    /** the sensor that generated this event. */
    public final Sensor sensor;
    /** length of array of values. */
    public final int accuracy;
    /** event timestamp. */
    public final long timestamp;
    /** sensor values. */
    public final float[] values;

    public SensorEvent(Sensor sensor, int accuracy, long timestamp, float[] values) {
        this.sensor = sensor;
        this.accuracy = accuracy;
        this.timestamp = timestamp;
        this.values = values;
    }
}