package android.content;

import android.os.Bundle;

/**
 * Minimal android.content.Intent implementation.
 *
 * Describes an operation to be performed (e.g. start an Activity). For
 * KuDroid's minimal framework, we store the component/class name and any
 * extras Bundle.
 */
public class Intent {
    /** Activity action. */
    public static final String ACTION_MAIN = "android.intent.action.MAIN";
    /** View action. */
    public static final String ACTION_VIEW = "android.intent.action.VIEW";
    /** Send action. */
    public static final String ACTION_SEND = "android.intent.action.SEND";

    private String mAction;
    private String mPackage;
    private String mClassName;
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
    }

    public Intent(String action, android.net.Uri uri) {
        mAction = action;
        mData = uri;
    }

    public Intent(Intent o) {
        mAction = o.mAction;
        mPackage = o.mPackage;
        mClassName = o.mClassName;
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
        return this;
    }

    public String getComponent() {
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
        return "Intent{action=" + mAction + ", component=" + mClassName + "}";
    }
}
