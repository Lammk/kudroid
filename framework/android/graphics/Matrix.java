package android.graphics;

/**
 * 3x3 matrix for transforming coordinates following AOSP specification.
 */
public class Matrix {

    public static final int MSCALE_X = 0;
    public static final int MSKEW_X  = 1;
    public static final int MTRANS_X = 2;
    public static final int MSKEW_Y  = 3;
    public static final int MSCALE_Y = 4;
    public static final int MTRANS_Y = 5;
    public static final int MPERSP_0 = 6;
    public static final int MPERSP_1 = 7;
    public static final int MPERSP_2 = 8;

    public enum ScaleToFit {
        FILL(0),
        START(1),
        CENTER(2),
        END(3);

        ScaleToFit(int nativeInt) {
            this.nativeInt = nativeInt;
        }
        final int nativeInt;
    }

    private final float[] mValues = new float[9];

    public Matrix() {
        reset();
    }

    public Matrix(Matrix src) {
        set(src);
    }

    public boolean isIdentity() {
        return mValues[MSCALE_X] == 1.0f && mValues[MSKEW_X] == 0.0f  && mValues[MTRANS_X] == 0.0f
            && mValues[MSKEW_Y] == 0.0f  && mValues[MSCALE_Y] == 1.0f && mValues[MTRANS_Y] == 0.0f
            && mValues[MPERSP_0] == 0.0f && mValues[MPERSP_1] == 0.0f && mValues[MPERSP_2] == 1.0f;
    }

    public boolean isAffine() {
        return mValues[MPERSP_0] == 0.0f && mValues[MPERSP_1] == 0.0f && mValues[MPERSP_2] == 1.0f;
    }

    public boolean rectStaysRect() {
        if (!isAffine()) return false;
        return (mValues[MSKEW_X] == 0.0f && mValues[MSKEW_Y] == 0.0f)
            || (mValues[MSCALE_X] == 0.0f && mValues[MSCALE_Y] == 0.0f);
    }

    public void reset() {
        mValues[MSCALE_X] = 1.0f;
        mValues[MSKEW_X]  = 0.0f;
        mValues[MTRANS_X] = 0.0f;
        mValues[MSKEW_Y]  = 0.0f;
        mValues[MSCALE_Y] = 1.0f;
        mValues[MTRANS_Y] = 0.0f;
        mValues[MPERSP_0] = 0.0f;
        mValues[MPERSP_1] = 0.0f;
        mValues[MPERSP_2] = 1.0f;
    }

    public void set(Matrix src) {
        if (src == null) {
            reset();
        } else {
            System.arraycopy(src.mValues, 0, mValues, 0, 9);
        }
    }

    public void setTranslate(float dx, float dy) {
        reset();
        mValues[MTRANS_X] = dx;
        mValues[MTRANS_Y] = dy;
    }

    public void setScale(float sx, float sy, float px, float py) {
        reset();
        mValues[MSCALE_X] = sx;
        mValues[MSCALE_Y] = sy;
        mValues[MTRANS_X] = px - sx * px;
        mValues[MTRANS_Y] = py - sy * py;
    }

    public void setScale(float sx, float sy) {
        reset();
        mValues[MSCALE_X] = sx;
        mValues[MSCALE_Y] = sy;
    }

    public void setRotate(float degrees, float px, float py) {
        double rad = Math.toRadians(degrees);
        float sin = (float) Math.sin(rad);
        float cos = (float) Math.cos(rad);
        setSinCos(sin, cos, px, py);
    }

    public void setRotate(float degrees) {
        double rad = Math.toRadians(degrees);
        float sin = (float) Math.sin(rad);
        float cos = (float) Math.cos(rad);
        setSinCos(sin, cos);
    }

    public void setSinCos(float sinValue, float cosValue, float px, float py) {
        reset();
        mValues[MSCALE_X] = cosValue;
        mValues[MSKEW_X]  = -sinValue;
        mValues[MSKEW_Y]  = sinValue;
        mValues[MSCALE_Y] = cosValue;
        mValues[MTRANS_X] = px - cosValue * px + sinValue * py;
        mValues[MTRANS_Y] = py - sinValue * px - cosValue * py;
    }

    public void setSinCos(float sinValue, float cosValue) {
        reset();
        mValues[MSCALE_X] = cosValue;
        mValues[MSKEW_X]  = -sinValue;
        mValues[MSKEW_Y]  = sinValue;
        mValues[MSCALE_Y] = cosValue;
    }

    public void setSkew(float kx, float ky, float px, float py) {
        reset();
        mValues[MSKEW_X]  = kx;
        mValues[MSKEW_Y]  = ky;
        mValues[MTRANS_X] = -kx * py;
        mValues[MTRANS_Y] = -ky * px;
    }

    public void setSkew(float kx, float ky) {
        reset();
        mValues[MSKEW_X] = kx;
        mValues[MSKEW_Y] = ky;
    }

    public void setConcat(Matrix a, Matrix b) {
        if (a == null || b == null) return;
        float[] vA = a.mValues;
        float[] vB = b.mValues;
        float[] res = new float[9];

        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                res[r * 3 + c] = vA[r * 3 + 0] * vB[0 * 3 + c]
                               + vA[r * 3 + 1] * vB[1 * 3 + c]
                               + vA[r * 3 + 2] * vB[2 * 3 + c];
            }
        }
        System.arraycopy(res, 0, mValues, 0, 9);
    }

    public boolean preTranslate(float dx, float dy) {
        Matrix m = new Matrix();
        m.setTranslate(dx, dy);
        preConcat(m);
        return true;
    }

    public boolean preScale(float sx, float sy, float px, float py) {
        Matrix m = new Matrix();
        m.setScale(sx, sy, px, py);
        preConcat(m);
        return true;
    }

    public boolean preScale(float sx, float sy) {
        Matrix m = new Matrix();
        m.setScale(sx, sy);
        preConcat(m);
        return true;
    }

    public boolean preRotate(float degrees, float px, float py) {
        Matrix m = new Matrix();
        m.setRotate(degrees, px, py);
        preConcat(m);
        return true;
    }

    public boolean preRotate(float degrees) {
        Matrix m = new Matrix();
        m.setRotate(degrees);
        preConcat(m);
        return true;
    }

    public boolean preSkew(float kx, float ky, float px, float py) {
        Matrix m = new Matrix();
        m.setSkew(kx, ky, px, py);
        preConcat(m);
        return true;
    }

    public boolean preSkew(float kx, float ky) {
        Matrix m = new Matrix();
        m.setSkew(kx, ky);
        preConcat(m);
        return true;
    }

    public boolean preConcat(Matrix other) {
        setConcat(this, other);
        return true;
    }

    public boolean postTranslate(float dx, float dy) {
        Matrix m = new Matrix();
        m.setTranslate(dx, dy);
        postConcat(m);
        return true;
    }

    public boolean postScale(float sx, float sy, float px, float py) {
        Matrix m = new Matrix();
        m.setScale(sx, sy, px, py);
        postConcat(m);
        return true;
    }

    public boolean postScale(float sx, float sy) {
        Matrix m = new Matrix();
        m.setScale(sx, sy);
        postConcat(m);
        return true;
    }

    public boolean postRotate(float degrees, float px, float py) {
        Matrix m = new Matrix();
        m.setRotate(degrees, px, py);
        postConcat(m);
        return true;
    }

    public boolean postRotate(float degrees) {
        Matrix m = new Matrix();
        m.setRotate(degrees);
        postConcat(m);
        return true;
    }

    public boolean postSkew(float kx, float ky, float px, float py) {
        Matrix m = new Matrix();
        m.setSkew(kx, ky, px, py);
        postConcat(m);
        return true;
    }

    public boolean postSkew(float kx, float ky) {
        Matrix m = new Matrix();
        m.setSkew(kx, ky);
        postConcat(m);
        return true;
    }

    public boolean postConcat(Matrix other) {
        setConcat(other, this);
        return true;
    }

    public boolean setRectToRect(RectF src, RectF dst, ScaleToFit stf) {
        if (dst == null || src == null) return false;
        if (src.isEmpty()) {
            reset();
            return false;
        }
        if (dst.isEmpty()) {
            reset();
            mValues[MSCALE_X] = 0;
            mValues[MSCALE_Y] = 0;
            return true;
        }

        float sx = dst.width() / src.width();
        float sy = dst.height() / src.height();

        if (stf != ScaleToFit.FILL) {
            if (sy < sx) {
                sx = sy;
            } else {
                sy = sx;
            }
        }

        float tx = dst.left - src.left * sx;
        float ty = dst.top - src.top * sy;

        if (stf == ScaleToFit.CENTER) {
            tx += (dst.width() - src.width() * sx) * 0.5f;
            ty += (dst.height() - src.height() * sy) * 0.5f;
        } else if (stf == ScaleToFit.END) {
            tx += dst.width() - src.width() * sx;
            ty += dst.height() - src.height() * sy;
        }

        reset();
        mValues[MSCALE_X] = sx;
        mValues[MSCALE_Y] = sy;
        mValues[MTRANS_X] = tx;
        mValues[MTRANS_Y] = ty;
        return true;
    }

    public boolean invert(Matrix inverse) {
        float det = mValues[MSCALE_X] * (mValues[MSCALE_Y] * mValues[MPERSP_2] - mValues[MTRANS_Y] * mValues[MPERSP_1])
                  - mValues[MSKEW_X]  * (mValues[MSKEW_Y]  * mValues[MPERSP_2] - mValues[MTRANS_Y] * mValues[MPERSP_0])
                  + mValues[MTRANS_X] * (mValues[MSKEW_Y]  * mValues[MPERSP_1] - mValues[MSCALE_Y] * mValues[MPERSP_0]);

        if (Math.abs(det) < 1e-12f) return false;

        if (inverse != null) {
            float invDet = 1.0f / det;
            float[] inv = new float[9];

            inv[MSCALE_X] = (mValues[MSCALE_Y] * mValues[MPERSP_2] - mValues[MTRANS_Y] * mValues[MPERSP_1]) * invDet;
            inv[MSKEW_X]  = (mValues[MTRANS_X] * mValues[MPERSP_1] - mValues[MSKEW_X]  * mValues[MPERSP_2]) * invDet;
            inv[MTRANS_X] = (mValues[MSKEW_X]  * mValues[MTRANS_Y] - mValues[MTRANS_X] * mValues[MSCALE_Y]) * invDet;

            inv[MSKEW_Y]  = (mValues[MTRANS_Y] * mValues[MPERSP_0] - mValues[MSKEW_Y]  * mValues[MPERSP_2]) * invDet;
            inv[MSCALE_Y] = (mValues[MSCALE_X] * mValues[MPERSP_2] - mValues[MTRANS_X] * mValues[MPERSP_0]) * invDet;
            inv[MTRANS_Y] = (mValues[MTRANS_X] * mValues[MSKEW_Y]  - mValues[MSCALE_X] * mValues[MTRANS_Y]) * invDet;

            inv[MPERSP_0] = (mValues[MSKEW_Y]  * mValues[MPERSP_1] - mValues[MSCALE_Y] * mValues[MPERSP_0]) * invDet;
            inv[MPERSP_1] = (mValues[MSKEW_X]  * mValues[MPERSP_0] - mValues[MSCALE_X] * mValues[MPERSP_1]) * invDet;
            inv[MPERSP_2] = (mValues[MSCALE_X] * mValues[MSCALE_Y] - mValues[MSKEW_X]  * mValues[MSKEW_Y])  * invDet;

            System.arraycopy(inv, 0, inverse.mValues, 0, 9);
        }
        return true;
    }

    public void mapPoints(float[] dst, int dstIndex, float[] src, int srcIndex, int pointCount) {
        if (dst == null || src == null) return;
        for (int i = 0; i < pointCount; i++) {
            float x = src[srcIndex + i * 2 + 0];
            float y = src[srcIndex + i * 2 + 1];

            float w = x * mValues[MPERSP_0] + y * mValues[MPERSP_1] + mValues[MPERSP_2];
            float invW = (w != 0.0f) ? (1.0f / w) : 1.0f;

            dst[dstIndex + i * 2 + 0] = (x * mValues[MSCALE_X] + y * mValues[MSKEW_X] + mValues[MTRANS_X]) * invW;
            dst[dstIndex + i * 2 + 1] = (x * mValues[MSKEW_Y]  + y * mValues[MSCALE_Y] + mValues[MTRANS_Y]) * invW;
        }
    }

    public void mapVectors(float[] dst, int dstIndex, float[] src, int srcIndex, int vectorCount) {
        if (dst == null || src == null) return;
        for (int i = 0; i < vectorCount; i++) {
            float x = src[srcIndex + i * 2 + 0];
            float y = src[srcIndex + i * 2 + 1];

            dst[dstIndex + i * 2 + 0] = x * mValues[MSCALE_X] + y * mValues[MSKEW_X];
            dst[dstIndex + i * 2 + 1] = x * mValues[MSKEW_Y]  + y * mValues[MSCALE_Y];
        }
    }

    public void mapPoints(float[] dst, float[] src) {
        if (dst == null || src == null) return;
        mapPoints(dst, 0, src, 0, Math.min(dst.length, src.length) / 2);
    }

    public void mapVectors(float[] dst, float[] src) {
        if (dst == null || src == null) return;
        mapVectors(dst, 0, src, 0, Math.min(dst.length, src.length) / 2);
    }

    public void mapPoints(float[] pts) {
        mapPoints(pts, pts);
    }

    public void mapVectors(float[] vecs) {
        mapVectors(vecs, vecs);
    }

    public boolean mapRect(RectF dst, RectF src) {
        if (dst == null || src == null) return false;
        float[] pts = new float[] {
            src.left, src.top,
            src.right, src.top,
            src.right, src.bottom,
            src.left, src.bottom
        };
        mapPoints(pts);
        float minX = pts[0], maxX = pts[0];
        float minY = pts[1], maxY = pts[1];
        for (int i = 1; i < 4; i++) {
            float x = pts[i * 2 + 0];
            float y = pts[i * 2 + 1];
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }
        dst.set(minX, minY, maxX, maxY);
        return rectStaysRect();
    }

    public boolean mapRect(RectF rect) {
        return mapRect(rect, rect);
    }

    public float mapRadius(float radius) {
        float[] pts = new float[] { 0, 0, radius, 0 };
        mapPoints(pts);
        float dx = pts[2] - pts[0];
        float dy = pts[3] - pts[1];
        return (float) Math.hypot(dx, dy);
    }

    public void getValues(float[] values) {
        if (values != null && values.length >= 9) {
            System.arraycopy(mValues, 0, values, 0, 9);
        }
    }

    public void setValues(float[] values) {
        if (values != null && values.length >= 9) {
            System.arraycopy(values, 0, mValues, 0, 9);
        }
    }

    @Override
    public boolean equals(Object obj) {
        if (!(obj instanceof Matrix)) return false;
        float[] otherValues = ((Matrix) obj).mValues;
        for (int i = 0; i < 9; i++) {
            if (mValues[i] != otherValues[i]) return false;
        }
        return true;
    }

    @Override
    public int hashCode() {
        int result = 1;
        for (float v : mValues) {
            result = 31 * result + Float.floatToIntBits(v);
        }
        return result;
    }

    public String toShortString() {
        StringBuilder sb = new StringBuilder(64);
        toShortString(sb);
        return sb.toString();
    }

    public void toShortString(StringBuilder sb) {
        float[] v = mValues;
        sb.append('[');
        sb.append(v[0]).append(", ").append(v[1]).append(", ").append(v[2]).append("][");
        sb.append(v[3]).append(", ").append(v[4]).append(", ").append(v[5]).append("][");
        sb.append(v[6]).append(", ").append(v[7]).append(", ").append(v[8]).append(']');
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder(64);
        sb.append("Matrix{");
        toShortString(sb);
        sb.append('}');
        return sb.toString();
    }
}
