package android.content;

import android.os.Bundle;

/**
 * minimal android.content.intent implementation.
 *
 * describes an operation to be performed (for example, starting an activity). for
 * kudroid minimal framework, we store component/class names and any
 * any additional packages.
 */
public class Intent {
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
}
