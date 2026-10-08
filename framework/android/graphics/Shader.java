package android.graphics;

public class Shader {
    public enum TileMode {
        CLAMP(0), REPEAT(1), MIRROR(2);
        TileMode(int nativeInt) { this.nativeInt = nativeInt; }
        final int nativeInt;
    }

    private Matrix mLocalMatrix;

    public boolean getLocalMatrix(Matrix localM) {
        if (mLocalMatrix != null && localM != null) {
            localM.set(mLocalMatrix);
            return !mLocalMatrix.isIdentity();
        }
        return false;
    }

    public void setLocalMatrix(Matrix localM) {
        if (localM == null || localM.isIdentity()) {
            mLocalMatrix = null;
        } else {
            if (mLocalMatrix == null) {
                mLocalMatrix = new Matrix();
            }
            mLocalMatrix.set(localM);
        }
    }
}
