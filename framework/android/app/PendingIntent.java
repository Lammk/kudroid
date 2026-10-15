package android.app;

import android.content.Context;
import android.content.Intent;

/**
 * emulate android.app.pendingintent.
 *
 * a token that grants another application permission to perform an operation. for
 * kudroid minimal framework, this is an emulation.
 */
public final class PendingIntent {
    /** flag: once. */
    public static final int FLAG_ONE_SHOT = 1;
    /** flag: do not create. */
    public static final int FLAG_NO_CREATE = 2;
    /** flag: cancel current. */
    public static final int FLAG_CANCEL_CURRENT = 4;
    /** flag: current update. */
    public static final int FLAG_UPDATE_CURRENT = 8;

    private final Intent mIntent;

    private PendingIntent(Intent intent) {
        mIntent = intent;
    }

    /**
     * get the pending intent of the activity.
     */
    public static PendingIntent getActivity(Context context, int requestCode,
                                            Intent intent, int flags) {
        return new PendingIntent(intent);
    }

    /**
     * receive the pending intent of the broadcast.
     */
    public static PendingIntent getBroadcast(Context context, int requestCode,
                                             Intent intent, int flags) {
        return new PendingIntent(intent);
    }

    /**
     * receive the service's pending intent.
     */
    public static PendingIntent getService(Context context, int requestCode,
                                           Intent intent, int flags) {
        return new PendingIntent(intent);
    }

    /**
     * returns the wrapped intent.
     */
    public Intent getIntent() {
        return mIntent;
    }

    /**
     * send pending intent (no-op).
     */
    public void send() {
    }

    public interface OnFinished {
    }

}