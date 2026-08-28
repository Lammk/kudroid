package android.widget;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.view.View;

public class ImageView extends View {
    public enum ScaleType {
        MATRIX,
        FIT_XY,
        FIT_START,
        FIT_CENTER,
        FIT_END,
        CENTER,
        CENTER_CROP,
        CENTER_INSIDE
    }

    private Drawable mDrawable;
    private ScaleType mScaleType = ScaleType.FIT_CENTER;

    public ImageView() { super(null); }
    public ImageView(Context context) { super(context); }
    public void setImageDrawable(Drawable drawable) { this.mDrawable = drawable; invalidate(); }
    public Drawable getDrawable() { return mDrawable; }
    public void setImageResource(int resId) { invalidate(); }
    public void setImageBitmap(Bitmap bm) { invalidate(); }
    public void setImageURI(Uri uri) { invalidate(); }
    public void setScaleType(ScaleType scaleType) { this.mScaleType = scaleType; }
    public ScaleType getScaleType() { return mScaleType; }
    public void setAdjustViewBounds(boolean adjustViewBounds) {}
    public void setMaxWidth(int maxWidth) {}
    public void setMaxHeight(int maxHeight) {}
    public void setColorFilter(int color) {}
    public void clearColorFilter() {}
}
