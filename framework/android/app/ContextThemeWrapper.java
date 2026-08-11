package android.app;

import android.content.Context;
import android.content.ContextWrapper;

/**
 * Minimal android.app.ContextThemeWrapper implementation.
 *
 * A ContextWrapper that allows modifying the theme. For KuDroid's minimal
 * framework, this is a thin wrapper around ContextWrapper.
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
