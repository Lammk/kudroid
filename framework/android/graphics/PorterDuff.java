package android.graphics;

/**
 * triển khai android.graphics.porterduff tối thiểu.
 *
 * định nghĩa các chế độ tổng hợp. đối với khuôn khổ tối thiểu của kudroid, cung cấp
 * enum mode được sử dụng bởi canvas.drawcolor.
 */
public class PorterDuff {
    /**
     * các chế độ tổng hợp.
     */
    public enum Mode {
        /** xóa đích. */
        CLEAR(0),
        /** vẽ nguồn. */
        SRC(1),
        /** vẽ đích. */
        DST(2),
        /** vẽ nguồn lên trên đích. */
        SRC_OVER(3),
        /** vẽ đích lên trên nguồn. */
        DST_OVER(4),
        /** giữ nguồn và đích ở nơi chúng chồng lên nhau. */
        SRC_IN(5),
        /** giữ đích ở nơi nó chồng lên nguồn. */
        DST_IN(6),
        /** giữ nguồn ở nơi nó không chồng lên đích. */
        SRC_OUT(7),
        /** giữ đích ở nơi nó không chồng lên nguồn. */
        DST_OUT(8),
        /** giữ nguồn ở nơi nó chồng lên, và đích ở những nơi khác. */
        SRC_ATOP(9),
        /** giữ đích ở nơi nó chồng lên, và nguồn ở những nơi khác. */
        DST_ATOP(10),
        /** loại trừ hoặc (xor). */
        XOR(11),
        /** thêm nguồn và đích. */
        ADD(12),
        /** nhân nguồn và đích. */
        MULTIPLY(13),
        /** pha trộn màn hình. */
        SCREEN(14),
        /** pha trộn lớp phủ. */
        OVERLAY(15),
        /** pha trộn làm tối. */
        DARKEN(16),
        /** pha trộn làm sáng. */
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
