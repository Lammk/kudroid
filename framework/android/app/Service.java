package android.app;

import android.content.ComponentCallbacks2;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.IBinder;

/**
 * A component that runs without a UI. The framework creates it on the main thread when it is
 * started or bound (see ActivityThread) and drives the lifecycle callbacks below.
 */
public abstract class Service extends ContextWrapper implements ComponentCallbacks2 {
    public static final int START_CONTINUATION_MASK = 0xf;
    public static final int START_STICKY_COMPATIBILITY = 0;
    public static final int START_STICKY = 1;
    public static final int START_NOT_STICKY = 2;
    public static final int START_REDELIVER_INTENT = 3;
    public static final int START_FLAG_REDELIVERY = 0x0001;
    public static final int START_FLAG_RETRY = 0x0002;
    public static final int STOP_FOREGROUND_REMOVE = 1 << 0;
    public static final int STOP_FOREGROUND_DETACH = 1 << 1;

    private ActivityThread mThread;
    private String mClassName;
    private Application mApplication;
    private boolean mStartCompatibility;
    private int mLastStartId;

    public Service() {
        super(null);
    }

    public final Application getApplication() {
        return mApplication;
    }

    public void onCreate() {
    }

    @Deprecated
    public void onStart(Intent intent, int startId) {
    }

    public int onStartCommand(Intent intent, int flags, int startId) {
        onStart(intent, startId);
        return mStartCompatibility ? START_STICKY_COMPATIBILITY : START_STICKY;
    }

    public void onDestroy() {
    }

    public void onConfigurationChanged(Configuration newConfig) {
    }

    public void onLowMemory() {
    }

    public void onTrimMemory(int level) {
    }

    public abstract IBinder onBind(Intent intent);

    public boolean onUnbind(Intent intent) {
        return false;
    }

    public void onRebind(Intent intent) {
    }

    public void onTaskRemoved(Intent rootIntent) {
    }

    public final void stopSelf() {
        stopSelf(-1);
    }

    public final void stopSelf(int startId) {
        if (mThread != null) {
            mThread.stopServiceSelf(this, startId);
        }
    }

    public final boolean stopSelfResult(int startId) {
        if (mThread == null) {
            return false;
        }
        return mThread.stopServiceSelf(this, startId);
    }

    public final void startForeground(int id, Notification notification) {
    }

    public final void startForeground(int id, Notification notification, int foregroundServiceType) {
    }

    public final void stopForeground(boolean removeNotification) {
    }

    public final void stopForeground(int notificationBehavior) {
    }

    public final int getForegroundServiceType() {
        return 0;
    }

    /** Called by the framework before onCreate. */
    public final void attach(Context context, ActivityThread thread, String className,
            Application application) {
        attachBaseContext(context);
        mThread = thread;
        mClassName = className;
        mApplication = application;
    }

    final String getClassNameInternal() {
        return mClassName;
    }

    final int nextStartId() {
        return ++mLastStartId;
    }

    final int lastStartId() {
        return mLastStartId;
    }
}
