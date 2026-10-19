package android.graphics;

/**
 * Xfermode is the base class for objects that are called to implement custom
 * "transfer-modes" in the drawing pipeline.
 */
public class Xfermode {
    /** Native blend mode value, as BlendMode stores it. */
    int porterDuffMode;

    public Xfermode() {}
}
