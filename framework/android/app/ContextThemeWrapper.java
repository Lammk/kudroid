package android.app;

import android.content.Context;
import android.content.ContextWrapper;

/**
 * minimal android.app.contextthemewrapper implementation.
 *
 * a contextwrapper that allows theme modification. for framework
 * kudroid's minimum, this is a thin wrapper around the contextwrapper.
 */
public class ContextThemeWrapper extends ContextWrapper {
    public ContextThemeWrapper() {
        super(null);
    }

    public ContextThemeWrapper(Context base) {
        super(base);
    }

    public ContextThemeWrapper(Context base, int themeResId) {
        super(base);
    }
}
