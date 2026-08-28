package android.graphics;

public class Shader {
    public enum TileMode {
        CLAMP(0), REPEAT(1), MIRROR(2);
        TileMode(int nativeInt) { this.nativeInt = nativeInt; }
        final int nativeInt;
    }
    public boolean getLocalMatrix(Matrix localM) { return false; }
    public void setLocalMatrix(Matrix localM) {}
}
