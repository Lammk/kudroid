package android.graphics.drawable;

/** A StateListDrawable whose items carry ids; transitions are accepted but not played. */
public class AnimatedStateListDrawable extends StateListDrawable {
    private final java.util.ArrayList<Integer> mIds = new java.util.ArrayList<>();

    public AnimatedStateListDrawable() {}

    public void addState(int[] stateSet, Drawable drawable, int id) {
        if (drawable == null) return;
        super.addState(stateSet, drawable);
        mIds.add(id);
    }

    public <T extends Drawable & Animatable> void addTransition(int fromId, int toId, T transition,
            boolean reversible) {}
}
