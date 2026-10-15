package android.graphics;

public class BitmapShader extends Shader {
    public final Bitmap mBitmap;
    public BitmapShader(Bitmap bitmap, TileMode tileX, TileMode tileY) {
        mBitmap = bitmap;
    }
}
