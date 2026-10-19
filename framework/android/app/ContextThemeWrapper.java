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
    private android.view.LayoutInflater mInflater;
    private Resources mResources;
    private android.content.res.Configuration mOverrideConfiguration;

    public void applyOverrideConfiguration(android.content.res.Configuration config) {
        if (mResources != null) throw new IllegalStateException("Resources already accessed");
        if (mOverrideConfiguration != null) throw new IllegalStateException("Override configuration already set");
        mOverrideConfiguration = new android.content.res.Configuration(config);
    }

    @Override
    public Resources getResources() {
        if (mResources == null) {
            Context base = getBaseContext();
            mResources = mOverrideConfiguration == null ? super.getResources()
                    : base.createConfigurationContext(mOverrideConfiguration).getResources();
        }
        return mResources;
    }

    @Override
    public android.content.res.AssetManager getAssets() { return getResources().getAssets(); }


    @Override
    public Object getSystemService(String name) {
        if (Context.LAYOUT_INFLATER_SERVICE.equals(name)) {
            if (mInflater == null) {
                Context base = getBaseContext();
                mInflater = base == null ? new android.view.LayoutInflater(this)
                        : android.view.LayoutInflater.from(base).cloneInContext(this);
            }
            return mInflater;
        }
        return super.getSystemService(name);
    }

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
        if (mTheme != null) onApplyThemeResource(mTheme, resid, false);
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
        Context base = getBaseContext();
        if (base != null) mTheme.setTo(base.getTheme());
        if (mThemeResource != 0) onApplyThemeResource(mTheme, mThemeResource, true);
        return mTheme;
    }

    /** Hook AOSP calls before the theme is first used; subclasses override it. */
    protected void onApplyThemeResource(Resources.Theme theme, int resid, boolean first) {
        theme.applyStyle(resid, true);
    }
}
