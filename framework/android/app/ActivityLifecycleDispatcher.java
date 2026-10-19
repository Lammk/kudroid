package android.app;

import java.util.ArrayList;
import android.os.Bundle;

final class ActivityLifecycleDispatcher {
    static final int CREATED = 0;
    static final int STARTED = 1;
    static final int RESUMED = 2;
    static final int PAUSED = 3;
    static final int STOPPED = 4;
    static final int SAVEINSTANCESTATE = 5;
    static final int DESTROYED = 6;
    static final int PRE = 0, NORMAL = 1, POST = 2;
    private final ArrayList<Application.ActivityLifecycleCallbacks> callbacks = new ArrayList<>();

    synchronized void add(Application.ActivityLifecycleCallbacks callback) {
        callbacks.add(callback);
    }

    synchronized void remove(Application.ActivityLifecycleCallbacks callback) {
        callbacks.remove(callback);
    }

    void dispatch(Activity activity, int event, int phase, Bundle state, boolean reverse) {
        Application.ActivityLifecycleCallbacks[] snapshot;
        synchronized (this) {
            snapshot = callbacks.toArray(new Application.ActivityLifecycleCallbacks[callbacks.size()]);
        }
        for (int n = 0; n < snapshot.length; n++) {
            Application.ActivityLifecycleCallbacks callback = snapshot[reverse ? snapshot.length - 1 - n : n];
            switch (event) {
                case CREATED:
                    if (phase == PRE) callback.onActivityPreCreated(activity, state);
                    else if (phase == POST) callback.onActivityPostCreated(activity, state);
                    else callback.onActivityCreated(activity, state);
                    break;
                case STARTED:
                    if (phase == PRE) callback.onActivityPreStarted(activity);
                    else if (phase == POST) callback.onActivityPostStarted(activity);
                    else callback.onActivityStarted(activity);
                    break;
                case RESUMED:
                    if (phase == PRE) callback.onActivityPreResumed(activity);
                    else if (phase == POST) callback.onActivityPostResumed(activity);
                    else callback.onActivityResumed(activity);
                    break;
                case PAUSED:
                    if (phase == PRE) callback.onActivityPrePaused(activity);
                    else if (phase == POST) callback.onActivityPostPaused(activity);
                    else callback.onActivityPaused(activity);
                    break;
                case STOPPED:
                    if (phase == PRE) callback.onActivityPreStopped(activity);
                    else if (phase == POST) callback.onActivityPostStopped(activity);
                    else callback.onActivityStopped(activity);
                    break;
                case SAVEINSTANCESTATE:
                    if (phase == PRE) callback.onActivityPreSaveInstanceState(activity, state);
                    else if (phase == POST) callback.onActivityPostSaveInstanceState(activity, state);
                    else callback.onActivitySaveInstanceState(activity, state);
                    break;
                case DESTROYED:
                    if (phase == PRE) callback.onActivityPreDestroyed(activity);
                    else if (phase == POST) callback.onActivityPostDestroyed(activity);
                    else callback.onActivityDestroyed(activity);
                    break;
            }
        }
    }
}
