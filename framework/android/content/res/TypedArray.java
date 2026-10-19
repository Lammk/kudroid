package android.content.res;

import android.util.TypedValue;

public class TypedArray {
    private final Resources mResources;
    private final int[] mAttrs;
    private final TypedValue[] mValues;
    private final Resources.Theme mTheme;
    private final android.util.AttributeSet mXml;

    public TypedArray() { this(null, null); }
    TypedArray(Resources resources, int[] attrs) {
        this(resources, attrs, new TypedValue[attrs == null ? 0 : attrs.length]);
    }
    TypedArray(Resources resources, int[] attrs, TypedValue[] values) {
        this(resources, attrs, values, null, null);
    }
    TypedArray(Resources resources, int[] attrs, TypedValue[] values, Resources.Theme theme,
            android.util.AttributeSet xml) {
        mResources = resources; mAttrs = attrs; mValues = values; mTheme = theme; mXml = xml;
    }
    private TypedValue value(int index) {
        if (index < 0 || index >= length()) throw new IndexOutOfBoundsException();
        return mValues[index];
    }
    public int length() { return mAttrs == null ? 0 : mAttrs.length; }
    public int getIndexCount() {
        int count = 0;
        for (int i = 0; i < length(); i++) if (hasValueOrEmpty(i)) count++;
        return count;
    }
    public int getIndex(int at) {
        if (at < 0) throw new IndexOutOfBoundsException();
        for (int i = 0; i < length(); i++) if (hasValueOrEmpty(i) && at-- == 0) return i;
        throw new IndexOutOfBoundsException();
    }
    public Resources getResources() { return mResources != null ? mResources : Resources.getSystem(); }
    public boolean hasValue(int index) {
        TypedValue v = value(index);
        return v != null && v.type != TypedValue.TYPE_NULL;
    }
    public boolean hasValueOrEmpty(int index) {
        TypedValue v = value(index);
        return v != null && (v.type != TypedValue.TYPE_NULL || v.data == TypedValue.DATA_NULL_EMPTY);
    }
    public int getType(int index) { TypedValue v = value(index); return v == null ? TypedValue.TYPE_NULL : v.type; }
    private boolean integer(TypedValue v) {
        return v != null && v.type >= TypedValue.TYPE_FIRST_INT && v.type <= TypedValue.TYPE_LAST_INT;
    }
    public boolean getBoolean(int index, boolean def) {
        TypedValue v = value(index);
        if (integer(v)) return v.data != 0;
        if (v != null && v.type == TypedValue.TYPE_STRING && v.string != null) {
            if ("true".equalsIgnoreCase(v.string.toString())) return true;
            if ("false".equalsIgnoreCase(v.string.toString())) return false;
        }
        return def;
    }
    public int getInt(int index, int def) {
        TypedValue v = value(index);
        if (integer(v)) return v.data;
        if (v != null && v.type == TypedValue.TYPE_STRING && v.string != null) {
            try { return Integer.decode(v.string.toString()); } catch (NumberFormatException ignored) {}
        }
        return def;
    }
    public int getInteger(int index, int def) { return getInt(index, def); }
    public float getFloat(int index, float def) {
        TypedValue v = value(index);
        if (integer(v)) return v.data;
        if (v != null && v.type == TypedValue.TYPE_FLOAT) return Float.intBitsToFloat(v.data);
        if (v != null && v.type == TypedValue.TYPE_STRING && v.string != null) {
            try { return Float.parseFloat(v.string.toString()); } catch (NumberFormatException ignored) {}
        }
        return def;
    }
    public int getColor(int index, int def) {
        TypedValue v = value(index);
        if (integer(v)) return v.data;
        // A color selector: the color it shows with no state set.
        if (v != null && v.type == TypedValue.TYPE_STRING && v.resourceId != 0) {
            ColorStateList list = getResources().getColorStateList(v.resourceId, mTheme);
            if (list != null) return list.getDefaultColor();
        }
        return def;
    }
    public ColorStateList getColorStateList(int index) {
        TypedValue v = value(index);
        if (v == null) return null;
        if (v.type >= TypedValue.TYPE_FIRST_COLOR_INT && v.type <= TypedValue.TYPE_LAST_COLOR_INT) {
            return ColorStateList.valueOf(v.data);
        }
        int id = getResourceId(index, 0);
        return id != 0 ? getResources().getColorStateList(id, mTheme) : null;
    }
    public float getDimension(int index, float def) {
        TypedValue v = value(index);
        return v != null && v.type == TypedValue.TYPE_DIMENSION ? v.getDimension(getResources().getDisplayMetrics()) : def;
    }
    public int getDimensionPixelOffset(int index, int def) {
        TypedValue v = value(index);
        return v != null && v.type == TypedValue.TYPE_DIMENSION
                ? TypedValue.complexToDimensionPixelOffset(v.data, getResources().getDisplayMetrics()) : def;
    }
    public int getDimensionPixelSize(int index, int def) {
        TypedValue v = value(index);
        return v != null && v.type == TypedValue.TYPE_DIMENSION
                ? TypedValue.complexToDimensionPixelSize(v.data, getResources().getDisplayMetrics()) : def;
    }
    public int getLayoutDimension(int index, String name) {
        if (!hasValue(index)) {
            throw new UnsupportedOperationException(
                    getPositionDescription() + ": You must supply a " + name + " attribute.");
        }
        return getLayoutDimension(index, 0);
    }
    public int getLayoutDimension(int index, int def) {
        TypedValue v = value(index);
        return integer(v) ? v.data : getDimensionPixelSize(index, def);
    }
    public float getFraction(int index, int base, int pbase, float def) {
        TypedValue v = value(index);
        return v != null && v.type == TypedValue.TYPE_FRACTION ? v.getFraction(base, pbase) : def;
    }
    public int getResourceId(int index, int def) {
        TypedValue v = value(index);
        if (v == null) return def;
        if (v.resourceId != 0) return v.resourceId;
        return v.type == TypedValue.TYPE_REFERENCE && v.data != 0 ? v.data : def;
    }
    public String getString(int index) {
        TypedValue v = value(index);
        CharSequence text = v == null ? null : v.coerceToString();
        return text == null ? null : text.toString();
    }
    public String getNonResourceString(int index) {
        TypedValue v = value(index);
        return v != null && v.type == TypedValue.TYPE_STRING && v.resourceId == 0 && v.string != null ? v.string.toString() : null;
    }
    public CharSequence getText(int index) { return getString(index); }
    public CharSequence[] getTextArray(int index) {
        int id = getResourceId(index, 0);
        return id == 0 ? null : getResources().getTextArray(id);
    }
    // A drawable KuDroid cannot load yet (bitmap, selector, shape, ...) reads as "no
    // drawable" instead of aborting the view that asked for it; the first occurrence of
    // each resource is logged so the unsupported kinds can be found.
    public android.graphics.drawable.Drawable getDrawable(int index) {
        TypedValue v = value(index);
        if (v != null && v.type >= TypedValue.TYPE_FIRST_COLOR_INT && v.type <= TypedValue.TYPE_LAST_COLOR_INT)
            return new android.graphics.drawable.ColorDrawable(v.data);
        int id = getResourceId(index, 0);
        if (id == 0) return null;
        try {
            return getResources().getDrawable(id, mTheme);
        } catch (Resources.NotFoundException e) {
            android.util.Log.w("TypedArray", "drawable 0x" + Integer.toHexString(id)
                    + " unavailable: " + e.getMessage());
            return null;
        }
    }
    public int getSourceResourceId(int index, int def) { return def; }
    public boolean getValue(int index, TypedValue output) {
        if (output == null) throw new NullPointerException();
        TypedValue v = value(index);
        if (v == null) return false;
        output.setTo(v);
        return hasValueOrEmpty(index);
    }
    public TypedValue peekValue(int index) { return hasValueOrEmpty(index) ? value(index) : null; }
    public String getPositionDescription() {
        return mXml != null ? mXml.getPositionDescription() : "<internal>";
    }
    public int getChangingConfigurations() { return 0; }
    public void recycle() {}
    public String toString() { return "TypedArray{length=" + length() + "}"; }
}
