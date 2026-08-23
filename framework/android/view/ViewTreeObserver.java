package android.view;

import java.util.ArrayList;
import java.util.List;

/**
 * android.view.ViewTreeObserver — đăng ký callback vào các mốc của vòng đời
 * layout/draw. Các list được dispatch thật từ ViewGroup khi layout/draw chạy.
 */
public final class ViewTreeObserver {
    /**
     * Gọi trước mỗi lần vẽ. Trả false để hoãn frame hiện tại.
     */
    public interface OnPreDrawListener {
        boolean onPreDraw();
    }

    /**
     * Gọi sau khi cây view đã layout xong.
     */
    public interface OnGlobalLayoutListener {
        void onGlobalLayout();
    }

    /**
     * Gọi khi chế độ touch của window đổi.
     */
    public interface OnTouchModeChangeListener {
        void onTouchModeChanged(boolean isInTouchMode);
    }

    /**
     * Gọi khi vùng scroll đổi.
     */
    public interface OnScrollChangedListener {
        void onScrollChanged();
    }

    /**
     * Gọi khi cửa sổ nhận/mất focus.
     */
    public interface OnWindowFocusChangeListener {
        void onWindowFocusChanged(boolean hasFocus);
    }

    private final List<OnPreDrawListener> mPreDraw = new ArrayList<OnPreDrawListener>();
    private final List<OnGlobalLayoutListener> mGlobalLayout = new ArrayList<OnGlobalLayoutListener>();
    private final List<OnTouchModeChangeListener> mTouchMode = new ArrayList<OnTouchModeChangeListener>();
    private final List<OnScrollChangedListener> mScroll = new ArrayList<OnScrollChangedListener>();
    private boolean mAlive = true;

    public void addOnPreDrawListener(OnPreDrawListener l) {
        if (l != null && !mPreDraw.contains(l)) mPreDraw.add(l);
    }

    public void removeOnPreDrawListener(OnPreDrawListener l) {
        mPreDraw.remove(l);
    }

    public void addOnGlobalLayoutListener(OnGlobalLayoutListener l) {
        if (l != null && !mGlobalLayout.contains(l)) mGlobalLayout.add(l);
    }

    public void removeOnGlobalLayoutListener(OnGlobalLayoutListener l) {
        mGlobalLayout.remove(l);
    }

    /** Tên cũ trước API 16, vẫn được app gọi. */
    public void removeGlobalOnLayoutListener(OnGlobalLayoutListener l) {
        mGlobalLayout.remove(l);
    }

    public void addOnTouchModeChangeListener(OnTouchModeChangeListener l) {
        if (l != null && !mTouchMode.contains(l)) mTouchMode.add(l);
    }

    public void removeOnTouchModeChangeListener(OnTouchModeChangeListener l) {
        mTouchMode.remove(l);
    }

    public void addOnScrollChangedListener(OnScrollChangedListener l) {
        if (l != null && !mScroll.contains(l)) mScroll.add(l);
    }

    public void removeOnScrollChangedListener(OnScrollChangedListener l) {
        mScroll.remove(l);
    }

    public boolean isAlive() {
        return mAlive;
    }

    /** Trả false nếu có listener nào hoãn frame. */
    public boolean dispatchOnPreDraw() {
        boolean proceed = true;
        for (int i = mPreDraw.size() - 1; i >= 0; --i) {
            if (!mPreDraw.get(i).onPreDraw()) proceed = false;
        }
        return proceed;
    }

    public void dispatchOnGlobalLayout() {
        for (int i = mGlobalLayout.size() - 1; i >= 0; --i) {
            mGlobalLayout.get(i).onGlobalLayout();
        }
    }

    public void dispatchOnTouchModeChanged(boolean isInTouchMode) {
        for (int i = mTouchMode.size() - 1; i >= 0; --i) {
            mTouchMode.get(i).onTouchModeChanged(isInTouchMode);
        }
    }

    public void dispatchOnScrollChanged() {
        for (int i = mScroll.size() - 1; i >= 0; --i) {
            mScroll.get(i).onScrollChanged();
        }
    }

    void kill() {
        mAlive = false;
        mPreDraw.clear();
        mGlobalLayout.clear();
        mTouchMode.clear();
        mScroll.clear();
    }

    public interface OnDrawListener {
    }

}
