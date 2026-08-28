package android.hardware;

import java.util.List;
import java.util.Collections;

public abstract class SensorManager {
    public static final int SENSOR_DELAY_FASTEST = 0;
    public static final int SENSOR_DELAY_GAME = 1;
    public static final int SENSOR_DELAY_UI = 2;
    public static final int SENSOR_DELAY_NORMAL = 3;

    public List<Sensor> getSensorList(int type) { return Collections.emptyList(); }
    public Sensor getDefaultSensor(int type) { return new Sensor(type, "DefaultSensor"); }
    public boolean registerListener(SensorEventListener listener, Sensor sensor, int samplingPeriodUs) { return true; }
    public void unregisterListener(SensorEventListener listener) {}
    public void unregisterListener(SensorEventListener listener, Sensor sensor) {}
}
