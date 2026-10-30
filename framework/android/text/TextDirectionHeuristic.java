package android.text;

/** Decides whether a run of characters is laid out right to left. */
public interface TextDirectionHeuristic {
    boolean isRtl(char[] array, int start, int count);
    boolean isRtl(CharSequence cs, int start, int count);
}
