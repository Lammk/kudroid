package android.content;

import android.os.Bundle;

/**
 * minimal android.content.intent implementation.
 *
 * describes an operation to be performed (for example, starting an activity). for
 * kudroid minimal framework, we store component/class names and any
 * any additional packages.
 */
public class Intent implements android.os.Parcelable, Cloneable {
    /** activity action. */
    public static final String ACTION_MAIN = "android.intent.action.MAIN";
    /** view action. */
    public static final String ACTION_VIEW = "android.intent.action.VIEW";
    /** send action. */
    public static final String ACTION_SEND = "android.intent.action.SEND";

    private String mAction;
    private String mPackage;
    private String mClassName;
    private ComponentName mComponent;
    private android.net.Uri mData;
    private Bundle mExtras;
    private int mFlags;

    public Intent() {
    }

    public Intent(String action) {
        mAction = action;
    }

    public Intent(Context packageContext, Class<?> cls) {
        mPackage = packageContext.getPackageName();
        mClassName = cls.getName();
        mComponent = new ComponentName(mPackage, mClassName);
    }

    public Intent(String action, android.net.Uri uri) {
        mAction = action;
        mData = uri;
    }

    public Intent(Intent o) {
        mAction = o.mAction;
        mPackage = o.mPackage;
        mClassName = o.mClassName;
        mComponent = o.mComponent;
        mData = o.mData;
        mFlags = o.mFlags;
        if (o.mExtras != null) {
            mExtras = new Bundle(o.mExtras);
        }
    }

    public String getAction() {
        return mAction;
    }

    public Intent setAction(String action) {
        mAction = action;
        return this;
    }

    public String getPackage() {
        return mPackage;
    }

    public Intent setPackage(String packageName) {
        mPackage = packageName;
        return this;
    }

    public android.net.Uri getData() {
        return mData;
    }

    public Intent setData(android.net.Uri data) {
        mData = data;
        return this;
    }

    public Intent setClass(Context packageContext, Class<?> cls) {
        mPackage = packageContext.getPackageName();
        mClassName = cls.getName();
        mComponent = new ComponentName(mPackage, mClassName);
        return this;
    }

    public Intent setClassName(Context packageContext, String className) {
        mPackage = packageContext.getPackageName();
        mClassName = className;
        mComponent = new ComponentName(mPackage, className);
        return this;
    }

    public Intent setClassName(String packageName, String className) {
        mPackage = packageName;
        mClassName = className;
        mComponent = new ComponentName(packageName, className);
        return this;
    }

    /**
     * The component this Intent targets, or null when it is implicit.
     *
     * Returned a bare class-name String before, which is the wrong type: the AOSP
     * signature is {@code ComponentName getComponent()}, and every caller either
     * passes the result straight to PackageManager or reads getClassName() off it.
     * A String meant that
     *
     *   getPackageManager().getActivityInfo(getIntent().getComponent(), GET_META_DATA)
     *
     * — the AGDK GameActivity idiom for finding its native library name — could not
     * resolve at all, so the whole call was auto-stubbed to null.
     */
    public ComponentName getComponent() {
        return mComponent;
    }

    public Intent setComponent(ComponentName component) {
        mComponent = component;
        if (component != null) {
            mPackage = component.getPackageName();
            mClassName = component.getClassName();
        }
        return this;
    }

    /** The target class name, for callers that want it without a ComponentName. */
    public String getClassName() {
        return mClassName;
    }

    public Bundle getExtras() {
        return mExtras;
    }

    public Intent putExtra(String name, String value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putString(name, value);
        return this;
    }

    public Intent putExtra(String name, int value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putInt(name, value);
        return this;
    }

    public Intent putExtra(String name, long value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putLong(name, value);
        return this;
    }

    public Intent putExtra(String name, boolean value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putBoolean(name, value);
        return this;
    }

    public Intent putExtra(String name, float value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putFloat(name, value);
        return this;
    }

    public Intent putExtra(String name, double value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putDouble(name, value);
        return this;
    }

    public Intent putExtras(Bundle extras) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putAll(extras);
        return this;
    }

    // Extra getters. All five real APKs in the corpus reference getStringExtra, getIntExtra
    // and getBooleanExtra — the putExtra half was here, the reading half was not, which made
    // the Bundle write-only from the app's point of view.
    //
    // Every one takes the app's default when the key is absent, rather than a fixed zero or
    // null. That is the whole contract of these methods: an app passes the value it wants
    // for "not present", and returning something else changes behaviour on the exact path
    // the default exists for.

    public String getStringExtra(String name) {
        return mExtras != null ? mExtras.getString(name) : null;
    }

    public int getIntExtra(String name, int defaultValue) {
        return mExtras != null ? mExtras.getInt(name, defaultValue) : defaultValue;
    }

    public long getLongExtra(String name, long defaultValue) {
        return mExtras != null ? mExtras.getLong(name, defaultValue) : defaultValue;
    }

    public boolean getBooleanExtra(String name, boolean defaultValue) {
        return mExtras != null ? mExtras.getBoolean(name, defaultValue) : defaultValue;
    }

    public float getFloatExtra(String name, float defaultValue) {
        return mExtras != null ? mExtras.getFloat(name, defaultValue) : defaultValue;
    }

    public double getDoubleExtra(String name, double defaultValue) {
        return mExtras != null ? mExtras.getDouble(name, defaultValue) : defaultValue;
    }

    public CharSequence getCharSequenceExtra(String name) {
        return getStringExtra(name);
    }

    public Bundle getBundleExtra(String name) {
        return mExtras != null ? mExtras.getBundle(name) : null;
    }

    public boolean hasExtra(String name) {
        return mExtras != null && mExtras.containsKey(name);
    }

    public Intent removeExtra(String name) {
        if (mExtras != null) mExtras.remove(name);
        return this;
    }

    /**
     * A URI form of this Intent, as ClipData.Item.coerceToText needs when a clip holds an
     * Intent. Not the full intent: scheme syntax that Android can parse back, which nothing
     * here does, so the readable form is more useful than an unparseable one.
     */
    public String toUri(int flags) {
        return toString();
    }

    public int getFlags() {
        return mFlags;
    }

    public Intent setFlags(int flags) {
        mFlags = flags;
        return this;
    }

    public Intent addFlags(int flags) {
        mFlags |= flags;
        return this;
    }

    @Override
    public String toString() {
        return "Intent{action=" + mAction + ", component=" +
               (mComponent != null ? mComponent.flattenToString() : mClassName) + "}";
    }

    public static final String CATEGORY_DEFAULT = "android.intent.category.DEFAULT";
    public static final String CATEGORY_LAUNCHER = "android.intent.category.LAUNCHER";
    public static final String CATEGORY_HOME = "android.intent.category.HOME";
    public static final String CATEGORY_BROWSABLE = "android.intent.category.BROWSABLE";

    public static final int FLAG_ACTIVITY_NEW_TASK = 0x10000000;
    public static final int FLAG_ACTIVITY_SINGLE_TOP = 0x20000000;
    public static final int FLAG_ACTIVITY_CLEAR_TOP = 0x04000000;
    public static final int FLAG_ACTIVITY_CLEAR_TASK = 0x00008000;
    public static final int FLAG_ACTIVITY_NO_ANIMATION = 0x00010000;
    public static final int FLAG_GRANT_READ_URI_PERMISSION = 0x00000001;
    public static final int FLAG_GRANT_WRITE_URI_PERMISSION = 0x00000002;

    private String mType;
    private final java.util.HashSet<String> mCategories = new java.util.HashSet<String>();

    public Intent putExtra(String name, Bundle value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putBundle(name, value);
        return this;
    }

    public Intent putExtra(String name, android.os.Parcelable value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putParcelable(name, value);
        return this;
    }

    public Intent putExtra(String name, java.io.Serializable value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putSerializable(name, value);
        return this;
    }

    public Intent putExtra(String name, String[] value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putStringArray(name, value);
        return this;
    }

    public Intent putExtra(String name, int[] value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putIntArray(name, value);
        return this;
    }

    public Intent putExtra(String name, boolean[] value) {
        if (mExtras == null) mExtras = new Bundle();
        mExtras.putBooleanArray(name, value);
        return this;
    }

    @SuppressWarnings("unchecked")
    public <T extends android.os.Parcelable> T getParcelableExtra(String name) {
        return mExtras != null ? (T) mExtras.getParcelable(name) : null;
    }

    public java.io.Serializable getSerializableExtra(String name) {
        return mExtras != null ? mExtras.getSerializable(name) : null;
    }

    public String[] getStringArrayExtra(String name) {
        return mExtras != null ? mExtras.getStringArray(name) : null;
    }

    public int[] getIntArrayExtra(String name) {
        return mExtras != null ? mExtras.getIntArray(name) : null;
    }

    public boolean[] getBooleanArrayExtra(String name) {
        return mExtras != null ? mExtras.getBooleanArray(name) : null;
    }

    public Intent addCategory(String category) {
        if (category != null) mCategories.add(category);
        return this;
    }

    public void removeCategory(String category) {
        if (category != null) mCategories.remove(category);
    }

    public boolean hasCategory(String category) {
        return category != null && mCategories.contains(category);
    }

    public java.util.Set<String> getCategories() {
        return mCategories;
    }

    public Intent setType(String type) {
        mType = type;
        return this;
    }

    public String getType() {
        return mType;
    }

    public Intent setDataAndType(android.net.Uri data, String type) {
        mData = data;
        mType = type;
        return this;
    }

    @Override
    public Object clone() {
        return new Intent(this);
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel parcel, int flags) {
        parcel.writeString(mAction);
        parcel.writeBundle(mExtras);
        parcel.writeInt(mFlags);
    }
}
