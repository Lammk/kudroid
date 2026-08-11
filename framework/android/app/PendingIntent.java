package android.app;

import android.content.Context;
import android.content.Intent;

/**
 * Stub android.app.PendingIntent.
 *
 * A token that grants another app permission to perform an operation. For
 * KuDroid's minimal framework, this is a stub.
 */
public final class PendingIntent {
    /** Flag: one shot. */
    public static final int FLAG_ONE_SHOT = 1;
    /** Flag: no create. */
    public static final int FLAG_NO_CREATE = 2;
    /** Flag: cancel current. */
    public static final int FLAG_CANCEL_CURRENT = 4;
    /** Flag: update current. */
    public static final int FLAG_UPDATE_CURRENT = 8;

    private final Intent mIntent;

    private PendingIntent(Intent intent) {
        mIntent = intent;
    }

    /**
     * Get an activity pending intent.
     */
    public static PendingIntent getActivity(Context context, int requestCode,
                                            Intent intent, int flags) {
        return new PendingIntent(intent);
    }

    /**
     * Get a broadcast pending intent.
     */
    public static PendingIntent getBroadcast(Context context, int requestCode,
                                             Intent intent, int flags) {
        return new PendingIntent(intent);
    }

    /**
     * Get a service pending intent.
     */
    public static PendingIntent getService(Context context, int requestCode,
                                           Intent intent, int flags) {
        return new PendingIntent(intent);
    }

    /**
     * Return the wrapped intent.
     */
    public Intent getIntent() {
        return mIntent;
    }

    /**
     * Send the pending intent (no-op).
     */
    public void send() {
    }
}