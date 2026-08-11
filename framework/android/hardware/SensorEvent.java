package android.hardware;

/**
 * Stub android.hardware.SensorEvent.
 *
 * Represents a sensor event. For KuDroid's minimal framework, this is a stub.
 */
public class SensorEvent {
    /** The sensor that generated this event. */
    public final Sensor sensor;
    /** The length of the values array. */
    public final int accuracy;
    /** The event timestamp. */
    public final long timestamp;
    /** The sensor values. */
    public final float[] values;

    public SensorEvent(Sensor sensor, int accuracy, long timestamp, float[] values) {
        this.sensor = sensor;
        this.accuracy = accuracy;
        this.timestamp = timestamp;
        this.values = values;
    }
}