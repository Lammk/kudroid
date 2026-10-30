package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

public class SearchView extends LinearLayout {
    public interface OnQueryTextListener {
        boolean onQueryTextSubmit(String query);
        boolean onQueryTextChange(String newText);
    }

    public interface OnCloseListener {
        boolean onClose();
    }

    public interface OnSuggestionListener {
        boolean onSuggestionSelect(int position);
        boolean onSuggestionClick(int position);
    }

    private CharSequence mQuery = "";
    private CharSequence mQueryHint = "";
    private boolean mIconified = true;
    private boolean mIconifiedByDefault = true;
    private boolean mSubmitButtonEnabled = false;
    private int mMaxWidth = Integer.MAX_VALUE;
    private OnQueryTextListener mOnQueryTextListener;
    private OnCloseListener mOnCloseListener;
    private OnClickListener mOnSearchClickListener;

    public SearchView(Context context) {
        this(context, null);
    }

    public SearchView(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public SearchView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public SearchView(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }

    public void setOnQueryTextListener(OnQueryTextListener listener) {
        mOnQueryTextListener = listener;
    }

    public void setOnCloseListener(OnCloseListener listener) {
        mOnCloseListener = listener;
    }

    public void setOnSearchClickListener(OnClickListener listener) {
        mOnSearchClickListener = listener;
    }

    public void setQuery(CharSequence query, boolean submit) {
        mQuery = query != null ? query : "";
        if (mOnQueryTextListener != null) {
            mOnQueryTextListener.onQueryTextChange(mQuery.toString());
            if (submit) {
                mOnQueryTextListener.onQueryTextSubmit(mQuery.toString());
            }
        }
    }

    public CharSequence getQuery() {
        return mQuery != null ? mQuery : "";
    }

    public void setQueryHint(CharSequence hint) {
        mQueryHint = hint;
    }

    public CharSequence getQueryHint() {
        return mQueryHint;
    }

    public void setIconifiedByDefault(boolean iconified) {
        mIconifiedByDefault = iconified;
    }

    public boolean isIconfiedByDefault() {
        return mIconifiedByDefault;
    }

    public void setIconified(boolean iconify) {
        if (iconify) {
            onCloseClicked();
        } else {
            onSearchClicked();
        }
    }

    public boolean isIconified() {
        return mIconified;
    }

    public void setSubmitButtonEnabled(boolean enabled) {
        mSubmitButtonEnabled = enabled;
    }

    public boolean isSubmitButtonEnabled() {
        return mSubmitButtonEnabled;
    }

    public void setMaxWidth(int maxpixels) {
        mMaxWidth = maxpixels;
        requestLayout();
    }

    public int getMaxWidth() {
        return mMaxWidth;
    }

    private void onSearchClicked() {
        mIconified = false;
        if (mOnSearchClickListener != null) {
            mOnSearchClickListener.onClick(this);
        }
    }

    private void onCloseClicked() {
        mIconified = true;
        if (mOnCloseListener != null) {
            mOnCloseListener.onClose();
        }
    }
}
