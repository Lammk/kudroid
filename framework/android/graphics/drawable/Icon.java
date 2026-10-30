package android.graphics.drawable;

import android.content.Context;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Parcel;
import android.os.Parcelable;

/** Icon description: what it points at is kept; nothing is rasterised here. */
public final class Icon implements Parcelable {
    public static final int TYPE_BITMAP = 1;
    public static final int TYPE_RESOURCE = 2;
    public static final int TYPE_DATA = 3;
    public static final int TYPE_URI = 4;
    public static final int TYPE_ADAPTIVE_BITMAP = 5;
    public static final int TYPE_URI_ADAPTIVE_BITMAP = 6;

    private final int mType;
    private String mPackage;
    private int mResId;
    private Bitmap mBitmap;
    private byte[] mData;
    private int mDataOffset;
    private int mDataLength;
    private String mUri;
    private android.content.res.ColorStateList mTintList;

    private Icon(int type) {
        mType = type;
    }

    public static Icon createWithResource(Context context, int resId) {
        if (context == null) throw new IllegalArgumentException("Context must not be null.");
        return createWithResource(context.getPackageName(), resId);
    }

    public static Icon createWithResource(String resPackage, int resId) {
        if (resPackage == null) throw new IllegalArgumentException("Resource package name must not be null.");
        Icon icon = new Icon(TYPE_RESOURCE);
        icon.mPackage = resPackage;
        icon.mResId = resId;
        return icon;
    }

    public static Icon createWithBitmap(Bitmap bits) {
        if (bits == null) throw new IllegalArgumentException("Bitmap must not be null.");
        Icon icon = new Icon(TYPE_BITMAP);
        icon.mBitmap = bits;
        return icon;
    }

    public static Icon createWithAdaptiveBitmap(Bitmap bits) {
        if (bits == null) throw new IllegalArgumentException("Bitmap must not be null.");
        Icon icon = new Icon(TYPE_ADAPTIVE_BITMAP);
        icon.mBitmap = bits;
        return icon;
    }

    public static Icon createWithData(byte[] data, int offset, int length) {
        if (data == null) throw new IllegalArgumentException("Data must not be null.");
        Icon icon = new Icon(TYPE_DATA);
        icon.mData = data;
        icon.mDataOffset = offset;
        icon.mDataLength = length;
        return icon;
    }

    public static Icon createWithContentUri(String uri) {
        if (uri == null) throw new IllegalArgumentException("Uri must not be null.");
        Icon icon = new Icon(TYPE_URI);
        icon.mUri = uri;
        return icon;
    }

    public static Icon createWithContentUri(Uri uri) {
        if (uri == null) throw new IllegalArgumentException("Uri must not be null.");
        return createWithContentUri(uri.toString());
    }

    public int getType() { return mType; }

    public int getResId() {
        if (mType != TYPE_RESOURCE) throw new IllegalStateException("called getResId() on " + this);
        return mResId;
    }

    public String getResPackage() {
        if (mType != TYPE_RESOURCE) throw new IllegalStateException("called getResPackage() on " + this);
        return mPackage;
    }

    public Bitmap getBitmap() {
        if (mType != TYPE_BITMAP && mType != TYPE_ADAPTIVE_BITMAP) {
            throw new IllegalStateException("called getBitmap() on " + this);
        }
        return mBitmap;
    }

    public Uri getUri() {
        return mUri != null ? Uri.parse(mUri) : null;
    }

    public Icon setTint(int tint) {
        mTintList = android.content.res.ColorStateList.valueOf(tint);
        return this;
    }

    public Icon setTintList(android.content.res.ColorStateList tintList) {
        mTintList = tintList;
        return this;
    }

    public Icon setTintMode(android.graphics.PorterDuff.Mode mode) {
        return this;
    }

    /** The drawable for a resource icon in this app's own package; null otherwise. */
    public Drawable loadDrawable(Context context) {
        if (mType == TYPE_RESOURCE && context != null && mResId != 0) {
            try {
                return context.getDrawable(mResId);
            } catch (RuntimeException ignored) {
                return null;
            }
        }
        return null;
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder("Icon(typ=").append(mType);
        if (mType == TYPE_RESOURCE) sb.append(" pkg=").append(mPackage).append(" id=0x").append(Integer.toHexString(mResId));
        else if (mType == TYPE_URI) sb.append(" uri=").append(mUri);
        return sb.append(")").toString();
    }

    @Override
    public int describeContents() { return 0; }

    @Override
    public void writeToParcel(Parcel dest, int flags) {}

    public static final Parcelable.Creator<Icon> CREATOR = new Parcelable.Creator<Icon>() {
        @Override
        public Icon createFromParcel(Parcel source) { return null; }

        @Override
        public Icon[] newArray(int size) { return new Icon[size]; }
    };
}
