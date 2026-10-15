package android.hardware;

public class SensorEvent {
    public final float[] values;
    public Sensor sensor;
    public int accuracy;
    public long timestamp;
    SensorEvent(int size) { values = new float[size]; }
}
