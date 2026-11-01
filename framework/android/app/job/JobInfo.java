package android.app.job;

import android.content.ComponentName;
import android.os.PersistableBundle;

public class JobInfo {
    public static final int NETWORK_TYPE_NONE = 0;
    public static final int NETWORK_TYPE_ANY = 1;
    public static final int NETWORK_TYPE_UNMETERED = 2;
    public static final int NETWORK_TYPE_NOT_ROAMING = 3;
    public static final int NETWORK_TYPE_CELLULAR = 4;
    public static final int NETWORK_TYPE_METERED = NETWORK_TYPE_CELLULAR;
    private final int id;
    private final ComponentName service;
    private final PersistableBundle extras;
    private final int networkType;
    private final boolean charging, idle;
    private final long latency;

    public JobInfo() { this(new Builder()); }
    private JobInfo(Builder builder) {
        id = builder.id;
        service = builder.service;
        extras = new PersistableBundle(builder.extras);
        networkType = builder.networkType;
        charging = builder.charging;
        idle = builder.idle;
        latency = builder.latency;
    }
    public int getId() { return id; }
    public ComponentName getService() { return service; }
    public PersistableBundle getExtras() { return extras; }
    public int getNetworkType() { return networkType; }
    public boolean isRequireCharging() { return charging; }
    public boolean isRequireDeviceIdle() { return idle; }
    public long getMinLatencyMillis() { return latency; }

    public static class Builder {
        private int id;
        private ComponentName service;
        private PersistableBundle extras = new PersistableBundle();
        private int networkType;
        private boolean charging, idle;
        private long latency;
        public Builder() {}
        public Builder(int id, ComponentName service) { this.id = id; this.service = service; }
        public Builder setExtras(PersistableBundle extras) { this.extras = extras; return this; }
        public Builder setRequiredNetworkType(int type) {
            if (type < NETWORK_TYPE_NONE || type > NETWORK_TYPE_CELLULAR)
                throw new IllegalArgumentException("Invalid network type");
            networkType = type;
            return this;
        }
        public Builder setRequiresCharging(boolean value) { charging = value; return this; }
        public Builder setRequiresDeviceIdle(boolean value) { idle = value; return this; }
        public Builder setMinimumLatency(long value) { latency = value; return this; }
        public JobInfo build() { return new JobInfo(this); }
    }
}
