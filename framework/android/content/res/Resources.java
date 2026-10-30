package android.content.res;

import android.util.AttributeSet;
// android.util.DisplayMetrics, explicitly.
//
// There used to be a second DisplayMetrics in this package. Being in the same package, it
// won name resolution here without an import, so getDisplayMetrics() returned
// android.content.res.DisplayMetrics — while every app references
// Resources.getDisplayMetrics()Landroid/util/DisplayMetrics;. The method looked present and
// the reference resolved to nothing: a NoSuchMethodError whose cause was the return type.
// Five of six APKs in the corpus needed it. The duplicate class is gone; this import is
// what keeps it from coming back by accident.
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Display;
import java.io.ByteArrayInputStream;
import java.io.InputStream;

public class Resources {
    private final DisplayMetrics mMetrics = new DisplayMetrics();
    private final Configuration mConfiguration = new Configuration();
    private final AssetManager mAssets;
    private volatile int mConfigurationRevision;
    private final Theme mTheme = new Theme(this);

    private static native int nativeGetIdentifier(String name, String type, String packageName);
    private static native String nativeGetResourceName(int id);
    private static native byte[] nativeReadResource(String path);

    private static native int[] nativeGetStyle(int id, String locale, Configuration config, int sdk);
    private static native String nativeGetPoolString(int index);
    private static native boolean nativeGetValue(int id, String locale, TypedValue output,
                                                  boolean resolve, Configuration config, int sdk);
    private static native int nativeGetDarkMode();

    private String localeName() {
        return mConfiguration.locale != null ? mConfiguration.locale.toString() : "";
    }

    boolean loadValue(int id, TypedValue output, boolean resolve) {
        return nativeGetValue(id, localeName(), output, resolve, mConfiguration,
                android.os.Build.VERSION.SDK_INT);
    }

    public Resources() {
        this(new AssetManager(), null, null);
    }

    public Resources(AssetManager assets, DisplayMetrics metrics, Configuration config) {
        mAssets = assets != null ? assets : new AssetManager();
        // A fresh Resources must report the live display, not zeros: boot
        // layout/DIP math divides by these before anyone calls setTo.
        if (metrics != null) mMetrics.setTo(metrics);
        else {
            try { new Display().getMetrics(mMetrics); } catch (Throwable ignored) {}
        }
        if (config != null) mConfiguration.setTo(config);
        else {
            mConfiguration.setToDefaults();
            mConfiguration.locale = java.util.Locale.getDefault();
            mConfiguration.touchscreen = Configuration.TOUCHSCREEN_FINGER;
            mConfiguration.keyboard = Configuration.KEYBOARD_NOKEYS;
            mConfiguration.keyboardHidden = Configuration.KEYBOARDHIDDEN_YES;
            mConfiguration.hardKeyboardHidden = Configuration.HARDKEYBOARDHIDDEN_YES;
            mConfiguration.navigation = Configuration.NAVIGATION_NONAV;
            mConfiguration.navigationHidden = Configuration.NAVIGATIONHIDDEN_YES;
            mConfiguration.screenLayout = Configuration.SCREENLAYOUT_SIZE_NORMAL;
            int nightMode = Configuration.UI_MODE_NIGHT_NO;
            try {
                if (nativeGetDarkMode() != 0) {
                    nightMode = Configuration.UI_MODE_NIGHT_YES;
                }
            } catch (Throwable ignored) {}
            mConfiguration.uiMode = Configuration.UI_MODE_TYPE_NORMAL | nightMode;
            mConfiguration.colorMode = Configuration.COLOR_MODE_WIDE_COLOR_GAMUT_NO | Configuration.COLOR_MODE_HDR_NO;
            deriveConfiguration(mConfiguration, mMetrics);
        }
    }
    public static Resources getSystem() { return new Resources(); }
    public DisplayMetrics getDisplayMetrics() { return mMetrics; }
    public Configuration getConfiguration() { return mConfiguration; }
    public AssetManager getAssets() { return mAssets; }
    public String getString(int id) {
        TypedValue value = new TypedValue();
        if (!loadValue(id, value, true) || value.type != TypedValue.TYPE_STRING || value.string == null) {
            throw new NotFoundException("string resource 0x" + Integer.toHexString(id));
        }
        return value.string.toString();
    }
    public String getString(int id, Object... formatArgs) {
        String value = getString(id);
        java.util.Locale locale = mConfiguration.locale != null
                ? mConfiguration.locale : java.util.Locale.getDefault();
        return String.format(locale, value, formatArgs);
    }
    public String[] getStringArray(int id) { return new String[0]; }
    public CharSequence getText(int id) { return getString(id); }
    public CharSequence getText(int id, CharSequence def) {
        TypedValue value = new TypedValue();
        return loadValue(id, value, true) && value.type == TypedValue.TYPE_STRING
                && value.string != null ? value.string : def;
    }
    public CharSequence[] getTextArray(int id) { return new CharSequence[0]; }
    public int[] getIntArray(int id) { return new int[0]; }
    // The name is "[package:][type/]entry"; defType and defPackage supply what it omits,
    // and a numeric string is taken as the id itself.
    public int getIdentifier(String name, String defType, String defPackage) {
        if (name == null) throw new NullPointerException("name is null");
        try {
            return Integer.parseInt(name);
        } catch (NumberFormatException ignored) {
        }
        String entry = name;
        String type = defType;
        String pkg = defPackage;
        int colon = entry.indexOf(':');
        if (colon >= 0) {
            pkg = entry.substring(0, colon);
            entry = entry.substring(colon + 1);
        }
        int slash = entry.indexOf('/');
        if (slash >= 0) {
            type = entry.substring(0, slash);
            entry = entry.substring(slash + 1);
        }
        if ("alertTitle".equals(entry) && ("id".equals(type) || type == null)
                && ("android".equals(pkg) || pkg == null)) {
            return 0x01020027;
        }
        if ("ok".equals(entry) && ("string".equals(type) || type == null)
                && ("android".equals(pkg) || pkg == null)) {
            return 0x0104000a;
        }
        if ("cancel".equals(entry) && ("string".equals(type) || type == null)
                && ("android".equals(pkg) || pkg == null)) {
            return 0x01040000;
        }
        return nativeGetIdentifier(name, defType != null ? defType : "",
                defPackage != null ? defPackage : "");
    }
    public int getColor(int id) {
        ColorStateList list = getColorStateList(id, null);
        if (list == null) throw new NotFoundException("color resource 0x" + Integer.toHexString(id));
        return list.getDefaultColor();
    }
    public int getColor(int id, Theme theme) {
        TypedValue value = new TypedValue();
        if (loadValue(id, value, true) && value.type == TypedValue.TYPE_STRING) {
            ColorStateList list = getColorStateList(id, theme);
            if (list != null) return list.getDefaultColor();
        }
        return getColor(id);
    }
    // A plain color, or a color selector read from its XML file; null when the id is unknown
    // or is not a color resource.
    public ColorStateList getColorStateList(int id) {
        return getColorStateList(id, null);
    }

    public ColorStateList getColorStateList(int id, Theme theme) {
        TypedValue value = new TypedValue();
        if (!loadValue(id, value, true)) return null;
        if (value.type >= TypedValue.TYPE_FIRST_COLOR_INT
                && value.type <= TypedValue.TYPE_LAST_COLOR_INT) {
            return ColorStateList.valueOf(value.data);
        }
        if (value.type == TypedValue.TYPE_STRING && value.string != null
                && value.string.toString().endsWith(".xml")) {
            XmlResourceParser parser = getXml(id);
            try {
                return ColorStateList.createFromXml(this, parser, theme);
            } catch (org.xmlpull.v1.XmlPullParserException | java.io.IOException e) {
                throw new NotFoundException("Invalid color state list " + value.string + ": "
                        + e.getMessage());
            } finally {
                parser.close();
            }
        }
        return null;
    }

    private TypedValue valueOf(int id, int type) throws NotFoundException {
        TypedValue value = new TypedValue();
        getValue(id, value, true);
        if (type == TypedValue.TYPE_FIRST_INT
                ? value.type >= TypedValue.TYPE_FIRST_INT && value.type <= TypedValue.TYPE_LAST_INT
                : value.type == type) {
            return value;
        }
        throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id)
                + " type #0x" + Integer.toHexString(value.type) + " is not valid");
    }
    public float getDimension(int id) throws NotFoundException {
        return TypedValue.complexToDimension(valueOf(id, TypedValue.TYPE_DIMENSION).data, mMetrics);
    }
    public int getDimensionPixelSize(int id) throws NotFoundException {
        return TypedValue.complexToDimensionPixelSize(
                valueOf(id, TypedValue.TYPE_DIMENSION).data, mMetrics);
    }
    public int getDimensionPixelOffset(int id) throws NotFoundException {
        return TypedValue.complexToDimensionPixelOffset(
                valueOf(id, TypedValue.TYPE_DIMENSION).data, mMetrics);
    }
    public boolean getBoolean(int id) throws NotFoundException {
        return valueOf(id, TypedValue.TYPE_FIRST_INT).data != 0;
    }
    public int getInteger(int id) throws NotFoundException {
        return valueOf(id, TypedValue.TYPE_FIRST_INT).data;
    }
    public float getFraction(int id, int base, int pbase) throws NotFoundException {
        return TypedValue.complexToFraction(
                valueOf(id, TypedValue.TYPE_FRACTION).data, base, pbase);
    }
    public InputStream openRawResource(int id) { return new ByteArrayInputStream(new byte[0]); }
    public byte[] readResource(String path) {
        return nativeReadResource(path);
    }

    public InputStream openRawResource(int id, TypedValue value) {
        try {
            if (value == null) {
                value = new TypedValue();
                getValue(id, value, true);
            }
            if (value.string != null) {
                byte[] data = nativeReadResource(value.string.toString());
                if (data != null && data.length > 0) {
                    return new ByteArrayInputStream(data);
                }
            }
        } catch (Throwable ignored) {}
        return new ByteArrayInputStream(new byte[0]);
    }
    public AssetFileDescriptor openRawResourceFd(int id) { return null; }

    public android.graphics.drawable.Drawable getDrawable(int id) { return getDrawable(id, null); }
    public android.graphics.drawable.Drawable getDrawable(int id, Theme theme) {
        TypedValue value = new TypedValue(); getValue(id, value, true);
        if (value.type >= 28 && value.type <= 31)
            return new android.graphics.drawable.ColorDrawable(value.data);
        if (value.type != TypedValue.TYPE_STRING || value.string == null)
            throw new NotFoundException("Invalid drawable resource 0x" + Integer.toHexString(id));
        String path = value.string.toString();
        if (!path.endsWith(".xml")) {
            byte[] data = nativeReadResource(path);
            if (data != null && data.length > 0) {
                android.graphics.Bitmap bm = android.graphics.BitmapFactory.decodeByteArray(data, 0, data.length);
                if (bm != null) {
                    return new android.graphics.drawable.BitmapDrawable(this, bm);
                }
            }
            android.util.Log.w("Resources", "Undecoded bitmap drawable " + path);
            return new android.graphics.drawable.BitmapDrawable();
        }
        XmlResourceParser parser = getXml(id);
        try {
            while (parser.next() != org.xmlpull.v1.XmlPullParser.START_TAG &&
                    parser.getEventType() != org.xmlpull.v1.XmlPullParser.END_DOCUMENT) {}
            return android.graphics.drawable.DrawableInflater.create(this, parser, theme);
        } catch (org.xmlpull.v1.XmlPullParserException | java.io.IOException e) {
            throw new NotFoundException("Invalid drawable XML " + path + ": " + e.getMessage());
        } finally { parser.close(); }
    }

    // "package:type/entry", from the application table or the framework table.
    public String getResourceName(int resid) throws NotFoundException {
        String name = nativeGetResourceName(resid);
        if (name == null) {
            throw new NotFoundException("Unable to find resource ID #0x" + Integer.toHexString(resid));
        }
        return name;
    }

    public String getResourceTypeName(int resid) throws NotFoundException {
        String name = getResourceName(resid);
        return name.substring(name.indexOf(':') + 1, name.indexOf('/'));
    }

    public String getResourceEntryName(int resid) throws NotFoundException {
        String name = getResourceName(resid);
        return name.substring(name.indexOf('/') + 1);
    }

    public String getResourcePackageName(int resid) throws NotFoundException {
        String name = getResourceName(resid);
        return name.substring(0, name.indexOf(':'));
    }

    public void getValue(int id, TypedValue outValue, boolean resolveRefs)
            throws NotFoundException {
        if (outValue == null) throw new NullPointerException();
        if (!loadValue(id, outValue, resolveRefs))
            throw new NotFoundException("resource id 0x" + Integer.toHexString(id));
    }

    public void getValue(String name, TypedValue outValue, boolean resolveRefs)
            throws NotFoundException {
        int id = getIdentifier(name, "string", null);
        getValue(id, outValue, resolveRefs);
    }

    public XmlResourceParser getXml(int id) throws NotFoundException {
        TypedValue value = new TypedValue(); getValue(id, value, true);
        if (value.type != TypedValue.TYPE_STRING || value.string == null)
            throw new NotFoundException("Not an XML resource 0x" + Integer.toHexString(id));
        byte[] data = nativeReadResource(value.string.toString());
        if (data == null) throw new NotFoundException("Missing XML " + value.string);
        try { return new CompiledXmlResourceParser(data); }
        catch (org.xmlpull.v1.XmlPullParserException e) {
            throw new NotFoundException("Invalid XML " + value.string + ": " + e.getMessage());
        }
    }

    public XmlResourceParser getLayout(int id) throws NotFoundException {
        return getXml(id);
    }

    public XmlResourceParser getAnimation(int id) throws NotFoundException {
        return getXml(id);
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

    // XML attributes and the element's style attribute only; there is no theme here.
    public TypedArray obtainAttributes(AttributeSet set, int[] attrs) {
        if (attrs == null) throw new NullPointerException();
        return resolveStyledAttributes(null, set, attrs, 0, 0);
    }

    /** Flattened style: (attr id, type, data) triples in ascending attr id order. */
    private static final class StyleData {
        final int[] data;
        StyleData(int[] data) { this.data = data; }
        int indexOf(int attr) {
            int lo = 0, hi = data.length / 3 - 1;
            while (lo <= hi) {
                int mid = (lo + hi) >>> 1;
                int key = data[mid * 3];
                if (key < attr) lo = mid + 1;
                else if (key > attr) hi = mid - 1;
                else return mid;
            }
            return -1;
        }
    }

    private final java.util.HashMap<Integer, StyleData> mStyleCache = new java.util.HashMap<>();

    private StyleData styleData(int id) {
        synchronized (mStyleCache) {
            StyleData cached = mStyleCache.get(id);
            if (cached != null) return cached;
        }
        int[] data = nativeGetStyle(id, localeName(), mConfiguration, android.os.Build.VERSION.SDK_INT);
        if (data == null) return null;
        StyleData built = new StyleData(data);
        synchronized (mStyleCache) { mStyleCache.put(id, built); }
        return built;
    }

    /** Copy the style item for `attr` into `out`; false when the style has none. */
    private boolean styleValue(StyleData style, int attr, TypedValue out) {
        if (style == null) return false;
        int index = style.indexOf(attr);
        if (index < 0) return false;
        out.type = style.data[index * 3 + 1];
        out.data = style.data[index * 3 + 2];
        out.resourceId = 0;
        out.string = null;
        out.assetCookie = 1;
        if (out.type == TypedValue.TYPE_STRING) out.string = nativeGetPoolString(out.data);
        return true;
    }

    /** XML attribute `attr` of the element `set` is positioned on, typed. */
    private static boolean xmlValue(AttributeSet set, int attr, TypedValue out) {
        if (set instanceof CompiledXmlResourceParser) {
            CompiledXmlResourceParser parser = (CompiledXmlResourceParser) set;
            int index = parser.indexOfAttributeResource(attr);
            if (index < 0) return false;
            TypedValue typed = parser.getAttributeTypedValue(index);
            if (typed == null) return false;
            out.setTo(typed);
            return true;
        }
        // Any other AttributeSet: recover what its string accessors give.
        for (int i = 0, n = set.getAttributeCount(); i < n; i++) {
            if (set.getAttributeNameResource(i) != attr) continue;
            int reference = set.getAttributeResourceValue(i, 0);
            if (reference != 0) {
                out.type = TypedValue.TYPE_REFERENCE; out.data = reference; return true;
            }
            String text = set.getAttributeValue(i);
            if (text == null) return false;
            out.string = text; out.type = TypedValue.TYPE_STRING; out.data = 0;
            return true;
        }
        return false;
    }

    /**
     * obtainStyledAttributes with Android's precedence: an attribute set directly in the
     * XML wins over the element's style="" attribute, then the theme's default style
     * attribute, then the default style resource, and last the theme itself.
     */
    TypedArray resolveStyledAttributes(Theme theme, AttributeSet set, int[] attrs,
            int defStyleAttr, int defStyleRes) {
        final int count = attrs.length;
        final TypedValue[] values = new TypedValue[count];
        int defStyle = defStyleRes;
        if (defStyleAttr != 0 && theme != null) {
            TypedValue style = theme.lookupAttribute(defStyleAttr);
            if (style != null && style.type == TypedValue.TYPE_REFERENCE) defStyle = style.data;
        }
        StyleData defBag = defStyle != 0 ? styleData(defStyle) : null;
        StyleData xmlBag = null;
        if (set != null) {
            int xmlStyle = set.getStyleAttribute();
            if (xmlStyle != 0) xmlBag = styleData(xmlStyle);
        }
        for (int i = 0; i < count; i++) {
            final int attr = attrs[i];
            TypedValue v = new TypedValue();
            boolean found = set != null && xmlValue(set, attr, v);
            if (!found) found = styleValue(xmlBag, attr, v);
            if (!found) found = styleValue(defBag, attr, v);
            if (!found && theme != null) {
                TypedValue fromTheme = theme.lookupAttribute(attr);
                if (fromTheme != null) { v.setTo(fromTheme); found = true; }
            }
            values[i] = found ? resolveStyled(theme, v) : v;
        }
        return new TypedArray(this, attrs, values, theme, set);
    }

    /** Follow ?attr and @ref chains the way AttributeResolution does. */
    private TypedValue resolveStyled(Theme theme, TypedValue value) {
        TypedValue current = value;
        for (int depth = 0; depth < 32; depth++) {
            if (current.type == TypedValue.TYPE_ATTRIBUTE) {
                TypedValue next = theme != null ? theme.lookupAttribute(current.data) : null;
                if (next == null) return nullValue();
                current = next;
                continue;
            }
            if (current.type == TypedValue.TYPE_REFERENCE) {
                if (current.data == 0) return nullValue();
                TypedValue resolved = new TypedValue();
                if (!loadValue(current.data, resolved, true)) {
                    // Not something a table can resolve (a file-backed framework resource):
                    // keep the reference so getResourceId still answers.
                    TypedValue kept = new TypedValue();
                    kept.setTo(current);
                    kept.resourceId = current.data;
                    return kept;
                }
                if (resolved.type == TypedValue.TYPE_ATTRIBUTE) { current = resolved; continue; }
                if (resolved.resourceId == 0) resolved.resourceId = current.data;
                return resolved;
            }
            return current;
        }
        return nullValue();
    }

    private static TypedValue nullValue() {
        TypedValue v = new TypedValue();
        v.type = TypedValue.TYPE_NULL;
        v.data = TypedValue.DATA_NULL_UNDEFINED;
        return v;
    }

    public TypedArray obtainTypedArray(int id) throws NotFoundException {
        return new TypedArray(this, new int[0]);
    }

    public void updateConfiguration(Configuration config, DisplayMetrics metrics) {
        if (metrics != null) mMetrics.setTo(metrics);
        if (config != null) mConfiguration.setTo(config);
        else if (metrics != null) deriveConfiguration(mConfiguration, mMetrics);
        synchronized (mStyleCache) { mStyleCache.clear(); }
        mConfigurationRevision++;
    }

    private static void deriveConfiguration(Configuration out, DisplayMetrics metrics) {
        float density = metrics.density > 0.0f ? metrics.density : 1.0f;
        int widthDp = Math.max(1, Math.round(metrics.widthPixels / density));
        int heightDp = Math.max(1, Math.round(metrics.heightPixels / density));
        out.densityDpi = metrics.densityDpi > 0
                ? metrics.densityDpi : Math.round(density * 160.0f);
        out.screenWidthDp = widthDp;
        out.screenHeightDp = heightDp;
        out.smallestScreenWidthDp = Math.min(widthDp, heightDp);
        out.orientation = widthDp > heightDp
                ? Configuration.ORIENTATION_LANDSCAPE : Configuration.ORIENTATION_PORTRAIT;
        int longDp = Math.max(widthDp, heightDp), shortDp = Math.min(widthDp, heightDp);
        int size = longDp < 470 ? Configuration.SCREENLAYOUT_SIZE_SMALL
                : longDp >= 960 && shortDp >= 720 ? Configuration.SCREENLAYOUT_SIZE_XLARGE
                : longDp >= 640 && shortDp >= 480 ? Configuration.SCREENLAYOUT_SIZE_LARGE
                : Configuration.SCREENLAYOUT_SIZE_NORMAL;
        int longMode = longDp >= 470 && longDp * 3 / 5 >= shortDp - 1
                ? Configuration.SCREENLAYOUT_LONG_YES : Configuration.SCREENLAYOUT_LONG_NO;
        int direction = android.text.TextUtils.getLayoutDirectionFromLocale(out.locale) == 1
                ? Configuration.SCREENLAYOUT_LAYOUTDIR_RTL : Configuration.SCREENLAYOUT_LAYOUTDIR_LTR;
        out.screenLayout = (out.screenLayout & ~(Configuration.SCREENLAYOUT_SIZE_MASK
                | Configuration.SCREENLAYOUT_LONG_MASK | Configuration.SCREENLAYOUT_LAYOUTDIR_MASK
                | Configuration.SCREENLAYOUT_ROUND_MASK)) | size | longMode | direction
                | Configuration.SCREENLAYOUT_ROUND_NO;
    }

    public final void flushLayoutCache() {}

    public static final class Theme {
        private final Resources mResources;
        private final java.util.HashMap<Integer, TypedValue> attributes = new java.util.HashMap<>();
        private final java.util.ArrayList<Integer> appliedStyles = new java.util.ArrayList<>();
        private final java.util.ArrayList<Boolean> appliedForces = new java.util.ArrayList<>();
        private int configurationRevision;

        Theme() { this(null); }
        Theme(Resources resources) { mResources = resources; }
        public Resources getResources() { return mResources != null ? mResources : Resources.getSystem(); }

        private TypedValue resolveValue(TypedValue input, boolean resolveRefs) {
            if (input == null) return null;
            TypedValue value = new TypedValue();
            value.setTo(input);
            java.util.HashSet<Integer> visited = new java.util.HashSet<>();
            for (int depth = 0; depth < 64; depth++) {
                if (value.type == TypedValue.TYPE_ATTRIBUTE) {
                    if (!visited.add(value.data)) return null;
                    TypedValue next = attributes.get(value.data);
                    if (next == null) return null;
                    value.setTo(next);
                    continue;
                }
                if (value.type == TypedValue.TYPE_REFERENCE && value.data == 0) {
                    value.type = TypedValue.TYPE_NULL;
                    value.resourceId = 0;
                    return value;
                }
                if (resolveRefs && value.type == TypedValue.TYPE_REFERENCE) {
                    TypedValue resolved = new TypedValue();
                    if (getResources().loadValue(value.data, resolved, true)) {
                        value.setTo(resolved);
                        if (value.type == TypedValue.TYPE_ATTRIBUTE) continue;
                    } else {
                        value.resourceId = value.data;
                    }
                }
                return value;
            }
            return null;
        }

        public synchronized boolean resolveAttribute(int id, TypedValue output, boolean resolveRefs) {
            ensureCurrentConfiguration();
            if (output == null) throw new NullPointerException();
            TypedValue value = resolveValue(attributes.get(id), resolveRefs);
            if (value == null) { output.setTo(new TypedValue()); return false; }
            output.setTo(value);
            return value.type != TypedValue.TYPE_NULL;
        }

        /** The theme's own value for `attr`, with ?attr chains followed; null when unset. */
        synchronized TypedValue lookupAttribute(int attr) {
            ensureCurrentConfiguration();
            TypedValue value = attributes.get(attr);
            for (int depth = 0; value != null && value.type == TypedValue.TYPE_ATTRIBUTE; depth++) {
                if (depth >= 32) return null;
                value = attributes.get(value.data);
            }
            return value;
        }

        public TypedArray obtainStyledAttributes(int[] attrs) {
            if (attrs == null) throw new NullPointerException();
            return getResources().resolveStyledAttributes(this, null, attrs, 0, 0);
        }

        public TypedArray obtainStyledAttributes(int resid, int[] attrs) {
            if (attrs == null) throw new NullPointerException();
            return getResources().resolveStyledAttributes(this, null, attrs, 0, resid);
        }

        public TypedArray obtainStyledAttributes(AttributeSet set, int[] attrs, int defStyleAttr, int defStyleRes) {
            if (attrs == null) throw new NullPointerException();
            return getResources().resolveStyledAttributes(this, set, attrs, defStyleAttr, defStyleRes);
        }

        public synchronized void applyStyle(int id, boolean force) {
            ensureCurrentConfiguration();
            if (applyStyleValues(id, force)) {
                appliedStyles.add(id);
                appliedForces.add(force);
            }
        }

        private boolean applyStyleValues(int id, boolean force) {
            Resources resources = getResources();
            StyleData style = resources.styleData(id);
            if (style == null) {
                android.util.Log.w("Resources", "Missing style 0x" + Integer.toHexString(id));
                return false;
            }
            int[] data = style.data;
            for (int i = 0; i + 2 < data.length; i += 3) {
                TypedValue old = attributes.get(data[i]);
                if (!force && old != null && old.type != TypedValue.TYPE_NULL) continue;
                TypedValue value = new TypedValue();
                resources.styleValue(style, data[i], value);
                attributes.put(data[i], value);
            }
            return true;
        }

        private void ensureCurrentConfiguration() {
            int revision = getResources().mConfigurationRevision;
            if (configurationRevision == revision) return;
            attributes.clear();
            for (int i = 0; i < appliedStyles.size(); i++) {
                applyStyleValues(appliedStyles.get(i), appliedForces.get(i));
            }
            configurationRevision = revision;
        }

        public void setTo(Theme other) {
            if (other == null) throw new NullPointerException();
            if (other == this) return;
            java.util.HashMap<Integer, TypedValue> copy = new java.util.HashMap<>();
            java.util.ArrayList<Integer> styles;
            java.util.ArrayList<Boolean> forces;
            synchronized (other) {
                other.ensureCurrentConfiguration();
                styles = new java.util.ArrayList<>(other.appliedStyles);
                forces = new java.util.ArrayList<>(other.appliedForces);
                for (java.util.Map.Entry<Integer, TypedValue> entry : other.attributes.entrySet()) {
                    TypedValue value = new TypedValue(); value.setTo(entry.getValue());
                    copy.put(entry.getKey(), value);
                }
            }
            synchronized (this) {
                attributes.clear(); attributes.putAll(copy);
                appliedStyles.clear(); appliedStyles.addAll(styles);
                appliedForces.clear(); appliedForces.addAll(forces);
                configurationRevision = getResources().mConfigurationRevision;
            }
        }
        public int getChangingConfigurations() { return 0; }
        public void dump(int priority, String tag, String prefix) {}
        public synchronized void rebase() {
            configurationRevision = getResources().mConfigurationRevision - 1;
            ensureCurrentConfiguration();
        }
        public String toString() { return "Resources$Theme{attributes=" + attributes.size() + "}"; }
    }

    public static class NotFoundException extends RuntimeException {
        public NotFoundException() { super(); }
        public NotFoundException(String name) { super(name); }
    }
}
