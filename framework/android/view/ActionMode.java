package android.view;

/**
 * android.view.ActionMode — contextual toolbar action mode.
 */
public abstract class ActionMode {
    /**
     * ActionMode lifecycle callback.
     */
    public interface Callback {
        boolean onCreateActionMode(ActionMode mode, Menu menu);

        boolean onPrepareActionMode(ActionMode mode, Menu menu);

        boolean onActionItemClicked(ActionMode mode, MenuItem item);

        void onDestroyActionMode(ActionMode mode);
    }

    /**
     * Callback API 23+: add hook to set toolbar position.
     */
    public interface Callback2 extends Callback {
        void onGetContentRect(ActionMode mode, View view, android.graphics.Rect outRect);
    }

    private Object mTag;
    private CharSequence mTitle;
    private CharSequence mSubtitle;
    private boolean mTitleOptionalHint;

    public void setTag(Object tag) {
        mTag = tag;
    }

    public Object getTag() {
        return mTag;
    }

    public void setTitle(CharSequence title) {
        mTitle = title;
    }

    public void setTitle(int resId) {
        mTitle = null;
    }

    public CharSequence getTitle() {
        return mTitle;
    }

    public void setSubtitle(CharSequence subtitle) {
        mSubtitle = subtitle;
    }

    public void setSubtitle(int resId) {
        mSubtitle = null;
    }

    public CharSequence getSubtitle() {
        return mSubtitle;
    }

    public void setTitleOptionalHint(boolean titleOptional) {
        mTitleOptionalHint = titleOptional;
    }

    public boolean getTitleOptionalHint() {
        return mTitleOptionalHint;
    }

    public boolean isTitleOptional() {
        return mTitleOptionalHint;
    }

    public void setCustomView(View view) {
    }

    public View getCustomView() {
        return null;
    }

    public abstract void invalidate();

    public abstract void finish();

    public abstract Menu getMenu();

    public abstract MenuInflater getMenuInflater();
}
