package android.text;

/** The standard heuristics; the first strongly directional character decides. */
public final class TextDirectionHeuristics {
    public static final TextDirectionHeuristic LTR = new Fixed(false);
    public static final TextDirectionHeuristic RTL = new Fixed(true);
    public static final TextDirectionHeuristic FIRSTSTRONG_LTR = new FirstStrong(false);
    public static final TextDirectionHeuristic FIRSTSTRONG_RTL = new FirstStrong(true);
    public static final TextDirectionHeuristic ANYRTL_LTR = new AnyRtl();
    public static final TextDirectionHeuristic LOCALE = FIRSTSTRONG_LTR;

    private TextDirectionHeuristics() {}

    /** Hebrew, Arabic, Syriac, Thaana and their presentation forms. */
    private static boolean isStrongRtl(char c) {
        return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF)
                || (c >= 0xFE70 && c <= 0xFEFF);
    }

    private static boolean isStrongLtr(char c) {
        return Character.isLetter(c) && !isStrongRtl(c);
    }

    private abstract static class Base implements TextDirectionHeuristic {
        @Override
        public boolean isRtl(char[] array, int start, int count) {
            return isRtl(new String(array, start, count), 0, count);
        }
    }

    private static final class Fixed extends Base {
        private final boolean mRtl;

        Fixed(boolean rtl) { mRtl = rtl; }

        @Override
        public boolean isRtl(CharSequence cs, int start, int count) { return mRtl; }
    }

    private static final class FirstStrong extends Base {
        private final boolean mDefaultRtl;

        FirstStrong(boolean defaultRtl) { mDefaultRtl = defaultRtl; }

        @Override
        public boolean isRtl(CharSequence cs, int start, int count) {
            for (int i = start; i < start + count; i++) {
                final char c = cs.charAt(i);
                if (isStrongRtl(c)) return true;
                if (isStrongLtr(c)) return false;
            }
            return mDefaultRtl;
        }
    }

    private static final class AnyRtl extends Base {
        @Override
        public boolean isRtl(CharSequence cs, int start, int count) {
            for (int i = start; i < start + count; i++) {
                if (isStrongRtl(cs.charAt(i))) return true;
            }
            return false;
        }
    }
}
