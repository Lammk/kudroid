package android.database;

/**
 * android.database.ContentObserver — a callback for content changes.
 *
 * All five real APKs in the corpus construct one with
 * {@code ContentObserver(Landroid/os/Handler;)V} and hand it to
 * ContentResolver.registerContentObserver. The generated stub had only a no-argument
 * constructor, so that reference resolved to nothing: apps failed while setting up their
 * observers, before doing anything an observer would report on.
 *
 * KuDroid has no content providers, so nothing ever calls onChange. The class exists so
 * registration succeeds and the app carries on — an app that cannot register an observer
 * usually treats that as fatal, while an observer that never fires is indistinguishable
 * from a provider whose data never changed.
 */
public class ContentObserver {
    private final android.os.Handler mHandler;

    /**
     * @param handler the Handler whose thread onChange should run on; may be null, which
     *                Android takes to mean "call me on the calling thread".
     */
    public ContentObserver(android.os.Handler handler) {
        mHandler = handler;
    }

    /** Kept for the no-argument form some code still uses. */
    public ContentObserver() {
        this(null);
    }

    /**
     * True if this observer wants changes made by its own process reported back.
     *
     * False, matching Android's default. An observer that says true and then acts on its own
     * writes can loop, so the conservative answer is also the correct default.
     */
    public boolean deliverSelfNotifications() {
        return false;
    }

    public void onChange(boolean selfChange) {
    }

    public void onChange(boolean selfChange, android.net.Uri uri) {
        onChange(selfChange);
    }

    public void onChange(boolean selfChange, android.net.Uri uri, int flags) {
        onChange(selfChange, uri);
    }

    /**
     * Dispatch a change to this observer on its Handler's thread.
     *
     * Not called by anything in KuDroid — there are no providers to report changes — but
     * present because it is public API and because an app may drive its own observers.
     */
    public final void dispatchChange(boolean selfChange, android.net.Uri uri) {
        if (mHandler == null) {
            onChange(selfChange, uri);
            return;
        }
        final boolean self = selfChange;
        final android.net.Uri changed = uri;
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                onChange(self, changed);
            }
        });
    }

    public final void dispatchChange(boolean selfChange) {
        dispatchChange(selfChange, null);
    }
}
