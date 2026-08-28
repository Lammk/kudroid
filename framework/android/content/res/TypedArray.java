package android.content.res;

import android.util.AttributeSet;
import android.util.TypedValue;

/**
 * android.content.res.TypedArray.
 *
 * Was an empty generated stub. Every {@code obtainStyledAttributes()} call site
 * expects to read values out of the result and then {@code recycle()} it, so a stub
 * meant a NoSuchMethodError in the middle of view inflation — and that path runs in
 * onCreate for any app built on AppCompat or androidx.
 *
 * KuDroid does not parse compiled resources, so no attribute is ever present. That
 * is represented honestly: {@code hasValue()} is false and every getter returns the
 * caller's own defValue. Callers are written for this case because on real Android
 * an attribute missing from the current theme behaves the same way. What must not
 * happen is throwing or inventing values, since the defaults are the app's own
 * considered fallbacks.
 */
public class TypedArray {

    private final Resources mResources;
    private final int[] mAttrs;

    public TypedArray() {
        this(null, null);
    }

    TypedArray(Resources resources, int[] attrs) {
        mResources = resources;
        mAttrs = attrs;
    }

    /** Number of attributes queried, which is what callers loop over. */
    public int length() {
        return mAttrs != null ? mAttrs.length : 0;
    }

    public int getIndexCount() {
        // No attribute carries a value, so there is nothing to iterate.
        return 0;
    }

    public int getIndex(int at) {
        return 0;
    }

    public Resources getResources() {
        return mResources != null ? mResources : Resources.getSystem();
    }

    /**
     * False for every index: nothing is resolved, so the caller keeps its default.
     *
     * Returning true here would be worse than useless — the caller would then read a
     * value that does not exist and act on a zero it never chose.
     */
    public boolean hasValue(int index) {
        return false;
    }

    public boolean hasValueOrEmpty(int index) {
        return false;
    }

    public int getType(int index) {
        return TypedValue.TYPE_NULL;
    }

    public boolean getBoolean(int index, boolean defValue) {
        return defValue;
    }

    public int getInt(int index, int defValue) {
        return defValue;
    }

    public int getInteger(int index, int defValue) {
        return defValue;
    }

    public float getFloat(int index, float defValue) {
        return defValue;
    }

    public int getColor(int index, int defValue) {
        return defValue;
    }

    public ColorStateList getColorStateList(int index) {
        return null;
    }

    public float getDimension(int index, float defValue) {
        return defValue;
    }

    public int getDimensionPixelOffset(int index, int defValue) {
        return defValue;
    }

    public int getDimensionPixelSize(int index, int defValue) {
        return defValue;
    }

    public int getLayoutDimension(int index, String name) {
        // ViewGroup.LayoutParams.WRAP_CONTENT: a view with no explicit size sizes to
        // its content, which is the behaviour that degrades most gracefully.
        return -2;
    }

    public int getLayoutDimension(int index, int defValue) {
        return defValue;
    }

    public float getFraction(int index, int base, int pbase, float defValue) {
        return defValue;
    }

    public int getResourceId(int index, int defValue) {
        return defValue;
    }

    public String getString(int index) {
        return null;
    }

    public String getNonResourceString(int index) {
        return null;
    }

    public CharSequence getText(int index) {
        return null;
    }

    public CharSequence[] getTextArray(int index) {
        return new CharSequence[0];
    }

    public android.graphics.drawable.Drawable getDrawable(int index) {
        return null;
    }

    public int getSourceResourceId(int index, int defaultValue) {
        return defaultValue;
    }

    public boolean getValue(int index, TypedValue outValue) {
        return false;
    }

    public String getPositionDescription() {
        return "<internal>";
    }

    public int getChangingConfigurations() {
        return 0;
    }

    /**
     * Release the array.
     *
     * A no-op because KuDroid does not pool these, but it must exist: every
     * obtain/recycle pair in app code calls it, usually in a finally block.
     */
    public void recycle() {
    }

    @Override
    public String toString() {
        return "TypedArray{length=" + length() + "}";
    }
}
