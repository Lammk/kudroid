package android.text.style;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.net.Uri;

/** Drawable span supporting the common ImageSpan construction paths. */
public class ImageSpan extends DynamicDrawableSpan {
    public static final int ALIGN_BOTTOM = DynamicDrawableSpan.ALIGN_BOTTOM;
    public static final int ALIGN_BASELINE = DynamicDrawableSpan.ALIGN_BASELINE;
    public static final int ALIGN_CENTER = DynamicDrawableSpan.ALIGN_CENTER;

    private Drawable mDrawable;
    private Uri mContentUri;
    private int mResourceId;
    private Context mContext;
    private String mSource;

    public ImageSpan(Bitmap bitmap) {
        this(null, bitmap, ALIGN_BOTTOM);
    }

    public ImageSpan(Bitmap bitmap, int verticalAlignment) {
        this(null, bitmap, verticalAlignment);
    }

    public ImageSpan(Context context, Bitmap bitmap) {
        this(context, bitmap, ALIGN_BOTTOM);
    }

    public ImageSpan(Context context, Bitmap bitmap, int verticalAlignment) {
        super(verticalAlignment);
        mContext = context;
        if (bitmap == null) return;
        mDrawable = context != null && context.getResources() != null
                ? new BitmapDrawable(context.getResources(), bitmap)
                : new BitmapDrawable(bitmap);
        int width = mDrawable.getIntrinsicWidth();
        int height = mDrawable.getIntrinsicHeight();
        mDrawable.setBounds(0, 0, width > 0 ? width : 0, height > 0 ? height : 0);
    }

    public ImageSpan(Drawable drawable) {
        this(drawable, ALIGN_BOTTOM);
    }

    public ImageSpan(Drawable drawable, int verticalAlignment) {
        super(verticalAlignment);
        mDrawable = drawable;
    }

    public ImageSpan(Drawable drawable, String source) {
        this(drawable, source, ALIGN_BOTTOM);
    }

    public ImageSpan(Drawable drawable, String source, int verticalAlignment) {
        super(verticalAlignment);
        mDrawable = drawable;
        mSource = source;
    }

    public ImageSpan(Context context, Uri uri) {
        this(context, uri, ALIGN_BOTTOM);
    }

    public ImageSpan(Context context, Uri uri, int verticalAlignment) {
        super(verticalAlignment);
        mContext = context;
        mContentUri = uri;
        mSource = uri != null ? uri.toString() : null;
    }

    public ImageSpan(Context context, int resourceId) {
        this(context, resourceId, ALIGN_BOTTOM);
    }

    public ImageSpan(Context context, int resourceId, int verticalAlignment) {
        super(verticalAlignment);
        mContext = context;
        mResourceId = resourceId;
    }

    @Override
    public Drawable getDrawable() {
        if (mDrawable != null) return mDrawable;
        if (mContentUri != null) return null;
        if (mContext == null) return null;
        Drawable drawable = mContext.getDrawable(mResourceId);
        if (drawable != null) {
            int width = drawable.getIntrinsicWidth();
            int height = drawable.getIntrinsicHeight();
            drawable.setBounds(0, 0, width > 0 ? width : 0, height > 0 ? height : 0);
        }
        mDrawable = drawable;
        return drawable;
    }

    public String getSource() {
        return mSource;
    }
}
