package android.graphics.drawable;

import android.util.StateSet;

import java.util.ArrayList;

/** Shows the first child whose state spec matches the drawable's current state. */
public class StateListDrawable extends DrawableContainer {
    private StateListState mStateListState;

    public StateListDrawable() {
        this(new StateListState());
    }

    private StateListDrawable(StateListState state) {
        mStateListState = state;
        setConstantState(state);
    }

    public void addState(int[] stateSet, Drawable drawable) {
        if (drawable == null) return;
        mStateListState.addStateSet(stateSet, drawable);
        onStateChange(getState());
    }

    @Override
    public boolean isStateful() {
        return true;
    }

    @Override
    protected boolean onStateChange(int[] stateSet) {
        final boolean changed = super.onStateChange(stateSet);
        int index = mStateListState.indexOfStateSet(stateSet);
        if (index < 0) index = mStateListState.indexOfStateSet(StateSet.WILD_CARD);
        return selectDrawable(index) || changed;
    }

    public int getStateCount() {
        return mStateListState.getChildCount();
    }

    public int[] getStateSet(int index) {
        return mStateListState.mStateSets.get(index);
    }

    public Drawable getStateDrawable(int index) {
        return mStateListState.getChild(index);
    }

    public int findStateDrawableIndex(int[] stateSet) {
        return mStateListState.indexOfStateSet(stateSet);
    }

    @Override
    public Drawable mutate() {
        return this;
    }

    static class StateListState extends DrawableContainerState {
        final ArrayList<int[]> mStateSets = new ArrayList<>();

        int addStateSet(int[] stateSet, Drawable drawable) {
            final int pos = addChild(drawable);
            mStateSets.add(stateSet != null ? stateSet : StateSet.WILD_CARD);
            return pos;
        }

        int indexOfStateSet(int[] stateSet) {
            for (int i = 0; i < mStateSets.size(); i++) {
                if (StateSet.stateSetMatches(mStateSets.get(i), stateSet)) return i;
            }
            return -1;
        }

        @Override
        public boolean isStateful() {
            return true;
        }

        @Override
        public Drawable newDrawable() {
            return new StateListDrawable(this);
        }
    }
}
