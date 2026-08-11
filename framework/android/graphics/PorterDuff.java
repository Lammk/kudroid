package android.graphics;

/**
 * Minimal android.graphics.PorterDuff implementation.
 *
 * Defines compositing modes. For KuDroid's minimal framework, provides the
 * Mode enum used by Canvas.drawColor.
 */
public class PorterDuff {
    /**
     * Compositing modes.
     */
    public enum Mode {
        /** Clear the destination. */
        CLEAR(0),
        /** Draw the source. */
        SRC(1),
        /** Draw the destination. */
        DST(2),
        /** Draw the source over the destination. */
        SRC_OVER(3),
        /** Draw the destination over the source. */
        DST_OVER(4),
        /** Keep the source and destination where they overlap. */
        SRC_IN(5),
        /** Keep the destination where it overlaps the source. */
        DST_IN(6),
        /** Keep the source where it does not overlap the destination. */
        SRC_OUT(7),
        /** Keep the destination where it does not overlap the source. */
        DST_OUT(8),
        /** Keep the source where it overlaps, and the destination elsewhere. */
        SRC_ATOP(9),
        /** Keep the destination where it overlaps, and the source elsewhere. */
        DST_ATOP(10),
        /** Exclusive or. */
        XOR(11),
        /** Add the source and destination. */
        ADD(12),
        /** Multiply the source and destination. */
        MULTIPLY(13),
        /** Screen blend. */
        SCREEN(14),
        /** Overlay blend. */
        OVERLAY(15),
        /** Darken blend. */
        DARKEN(16),
        /** Lighten blend. */
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
