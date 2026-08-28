package android.content.res;

import android.util.AttributeSet;
import android.util.TypedValue;
import java.io.ByteArrayInputStream;
import java.io.InputStream;

public class Resources {
    private final DisplayMetrics mMetrics = new DisplayMetrics();
    private final Configuration mConfiguration = new Configuration();
    private final AssetManager mAssets = new AssetManager();
    private final Theme mTheme = new Theme(this);

    public Resources() {}
    public Resources(AssetManager assets, DisplayMetrics metrics, Configuration config) {
        if (metrics != null) mMetrics.setTo(metrics);
        if (config != null) mConfiguration.setTo(config);
    }
    public static Resources getSystem() { return new Resources(); }
    public DisplayMetrics getDisplayMetrics() { return mMetrics; }
    public Configuration getConfiguration() { return mConfiguration; }
    public AssetManager getAssets() { return mAssets; }
    public String getString(int id) { return ""; }
    public String getString(int id, Object... formatArgs) { return ""; }
    public String[] getStringArray(int id) { return new String[0]; }
    public CharSequence getText(int id) { return ""; }
    public CharSequence getText(int id, CharSequence def) { return def; }
    public CharSequence[] getTextArray(int id) { return new CharSequence[0]; }
    public int[] getIntArray(int id) { return new int[0]; }
    public int getIdentifier(String name, String defType, String defPackage) { return 0; }
    public int getColor(int id) { return 0xFF000000; }
    public int getColor(int id, Theme theme) { return 0xFF000000; }
    public ColorStateList getColorStateList(int id) { return null; }
    public ColorStateList getColorStateList(int id, Theme theme) { return null; }
    public float getDimension(int id) { return 0.0f; }
    public int getDimensionPixelSize(int id) { return 0; }
    public int getDimensionPixelOffset(int id) { return 0; }
    public boolean getBoolean(int id) { return false; }
    public int getInteger(int id) { return 0; }
    public float getFraction(int id, int base, int pbase) { return 0.0f; }
    public InputStream openRawResource(int id) { return new ByteArrayInputStream(new byte[0]); }
    public InputStream openRawResource(int id, TypedValue value) {
        return new ByteArrayInputStream(new byte[0]);
    }
    public AssetFileDescriptor openRawResourceFd(int id) { return null; }

    public android.graphics.drawable.Drawable getDrawable(int id) { return null; }
    public android.graphics.drawable.Drawable getDrawable(int id, Theme theme) { return null; }

    /**
     * Resource name lookup.
     *
     * KuDroid does not parse compiled resources, so nothing can be named. Android
     * throws NotFoundException for an unknown id and callers are written for that;
     * returning a made-up name would be worse, since these end up in log messages
     * and crash reports that then point at a resource that does not exist.
     */
    public String getResourceName(int resid) throws NotFoundException {
        throw new NotFoundException("resource id 0x" + Integer.toHexString(resid));
    }

    public String getResourceTypeName(int resid) throws NotFoundException {
        throw new NotFoundException("resource id 0x" + Integer.toHexString(resid));
    }

    public String getResourceEntryName(int resid) throws NotFoundException {
        throw new NotFoundException("resource id 0x" + Integer.toHexString(resid));
    }

    public String getResourcePackageName(int resid) throws NotFoundException {
        throw new NotFoundException("resource id 0x" + Integer.toHexString(resid));
    }

    public void getValue(int id, TypedValue outValue, boolean resolveRefs)
            throws NotFoundException {
        if (outValue != null) outValue.type = TypedValue.TYPE_NULL;
    }

    public void getValue(String name, TypedValue outValue, boolean resolveRefs)
            throws NotFoundException {
        if (outValue != null) outValue.type = TypedValue.TYPE_NULL;
    }

    public XmlResourceParser getXml(int id) throws NotFoundException {
        return new XmlResourceParser();
    }

    public XmlResourceParser getLayout(int id) throws NotFoundException {
        return new XmlResourceParser();
    }

    public XmlResourceParser getAnimation(int id) throws NotFoundException {
        return new XmlResourceParser();
    }

    /** The theme this Resources hands out; never null, so callers can chain. */
    public Theme newTheme() { return new Theme(this); }

    /**
     * The shared default theme.
     *
     * Context.getTheme() returns this when nothing has overridden it. Public because
     * android.content and android.content.res are separate packages.
     */
    public Theme getDefaultTheme() { return mTheme; }

    public TypedArray obtainAttributes(AttributeSet set, int[] attrs) {
        return new TypedArray(this, attrs);
    }

    public TypedArray obtainTypedArray(int id) throws NotFoundException {
        return new TypedArray(this, new int[0]);
    }

    public void updateConfiguration(Configuration config, DisplayMetrics metrics) {
        if (config != null) mConfiguration.setTo(config);
        if (metrics != null) mMetrics.setTo(metrics);
    }

    public final void flushLayoutCache() {}

    /**
     * A resolved theme.
     *
     * This class did not exist, so {@code Activity.getTheme()} was auto-stubbed to
     * null and anything calling through it died with a NullPointerException. That is
     * not an edge case: androidx.core.splashscreen calls
     * {@code getTheme().resolveAttribute(...)} from its install() path, which apps
     * invoke as the first statement of onCreate, and every AppCompat activity calls
     * {@code getTheme().obtainStyledAttributes(...)} while inflating.
     *
     * KuDroid resolves no attributes, so resolveAttribute() reports failure and
     * obtainStyledAttributes() returns an empty TypedArray. Both are states real
     * Android produces for an attribute absent from the current theme, so callers
     * already handle them — unlike a null theme, which nothing handles.
     */
    public static final class Theme {

        private final Resources mResources;

        Theme() { mResources = null; }

        Theme(Resources resources) { mResources = resources; }

        public Resources getResources() {
            return mResources != null ? mResources : Resources.getSystem();
        }

        /**
         * Look up an attribute in this theme.
         *
         * Returns false — not found — and leaves outValue as TYPE_NULL. Callers test
         * the return value before reading, which is exactly why reporting failure is
         * safe and claiming success would not be.
         */
        public boolean resolveAttribute(int resid, TypedValue outValue, boolean resolveRefs) {
            if (outValue != null) {
                outValue.type = TypedValue.TYPE_NULL;
                outValue.data = TypedValue.DATA_NULL_UNDEFINED;
                outValue.resourceId = 0;
                outValue.string = null;
            }
            return false;
        }

        public TypedArray obtainStyledAttributes(int[] attrs) {
            return new TypedArray(mResources, attrs);
        }

        public TypedArray obtainStyledAttributes(int resid, int[] attrs)
                throws NotFoundException {
            return new TypedArray(mResources, attrs);
        }

        public TypedArray obtainStyledAttributes(AttributeSet set, int[] attrs,
                                                 int defStyleAttr, int defStyleRes) {
            return new TypedArray(mResources, attrs);
        }

        public void applyStyle(int resId, boolean force) {}

        public void setTo(Theme other) {}

        public int getChangingConfigurations() { return 0; }

        public void dump(int priority, String tag, String prefix) {}

        public void rebase() {}

        @Override
        public String toString() {
            return "Resources$Theme{}";
        }
    }

    public static class NotFoundException extends RuntimeException {
        public NotFoundException() { super(); }
        public NotFoundException(String name) { super(name); }
    }
}
