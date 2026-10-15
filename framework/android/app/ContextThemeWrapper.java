package android.app;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.res.Resources;

/**
 * A ContextWrapper that carries its own theme.
 *
 * The point of the class is the theme override, which was missing entirely: this was
 * a bare wrapper, so an Activity that called setTheme() then getTheme() got whatever
 * the base Context had. Activity extends this, and AppCompat sets a theme during
 * onCreate before inflating, so the override has to be honoured here or every styled
 * view resolves against the wrong theme.
 */
public class ContextThemeWrapper extends ContextWrapper {

    private int mThemeResource;
    private Resources.Theme mTheme;

    public ContextThemeWrapper() {
        super(null);
    }

    public ContextThemeWrapper(Context base) {
        super(base);
    }

    public ContextThemeWrapper(Context base, int themeResId) {
        super(base);
        mThemeResource = themeResId;
    }

    public ContextThemeWrapper(Context base, Resources.Theme theme) {
        super(base);
        mTheme = theme;
    }

    @Override
    public void setTheme(int resid) {
        if (mThemeResource == resid) return;
        mThemeResource = resid;
        // Drop the cached theme so the next getTheme() rebuilds with the new style,
        // matching AOSP; keeping it would silently ignore the change.
        mTheme = null;
    }

    public int getThemeResId() {
        return mThemeResource;
    }

    @Override
    public Resources.Theme getTheme() {
        if (mTheme != null) return mTheme;
        Resources r = getResources();
        if (r == null) r = Resources.getSystem();
        mTheme = r.newTheme();
        if (mThemeResource != 0) mTheme.applyStyle(mThemeResource, true);
        return mTheme;
    }

    /** Hook AOSP calls before the theme is first used; subclasses override it. */
    protected void onApplyThemeResource(Resources.Theme theme, int resid, boolean first) {
    }
}
