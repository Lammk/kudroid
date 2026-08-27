package android.graphics;

/**
 * minimal android.graphics.porterduff implementation.
 *
 * defines aggregation modes. for kudroid minimal framework, provided
 * enum mode used by canvas.drawcolor.
 */
public class PorterDuff {
    /**
     * synthesis modes.
     */
    public enum Mode {
        /** delete destination. */
        CLEAR(0),
        /** draw source. */
        SRC(1),
        /** draw target. */
        DST(2),
        /** draws the source on top of the destination. */
        SRC_OVER(3),
        /** paints the destination on top of the source. */
        DST_OVER(4),
        /** holds the source and destination where they overlap. */
        SRC_IN(5),
        /** keeps the destination where it overlaps the source. */
        DST_IN(6),
        /** keeps the source where it does not overlap the destination. */
        SRC_OUT(7),
        /** keeps the destination where it does not overlap the source. */
        DST_OUT(8),
        /** keeps the source where it overlaps, and the destination elsewhere. */
        SRC_ATOP(9),
        /** keeps the destination where it overlaps, and the source elsewhere. */
        DST_ATOP(10),
        /** exclude or (xor). */
        XOR(11),
        /** adds source and destination. */
        ADD(12),
        /** multiply the source and destination. */
        MULTIPLY(13),
        /** screen blending. */
        SCREEN(14),
        /** overlay blend. */
        OVERLAY(15),
        /** darkening blend. */
        DARKEN(16),
        /** brighten blend. */
        LIGHTEN(17);

        private final int mValue;

        Mode(int value) {
            mValue = value;
        }

        public int getValue() {
            return mValue;
        }
    }

    private PorterDuff() {
    }
}
