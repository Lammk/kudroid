package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.os.Bundle;

/**
 * triển khai android.view.view tối thiểu.
 *
 * lớp cơ sở cho tất cả các tiện ích ui. đối với khuôn khổ tối thiểu của kudroid, điều này
 * cung cấp các mô phỏng bố cục/vẽ cơ bản.
 */
public class View {
    /** khả năng hiển thị của view: hiển thị. */
    public static final int VISIBLE = 0;
    /** khả năng hiển thị của view: vô hình. */
    public static final int INVISIBLE = 4;
    /** khả năng hiển thị của view: biến mất. */
    public static final int GONE = 8;

    /** chế độ đặc tả đo lường: không được chỉ định. */
    public static final int UNSPECIFIED = 0;
    /** chế độ đặc tả đo lường: chính xác. */
    public static final int EXACTLY = 1;
    /** chế độ đặc tả đo lường: nhiều nhất. */
    public static final int AT_MOST = 2;

    protected final Context mContext;
    private int mLeft;
    private int mTop;
    private int mRight;
    private int mBottom;
    private int mVisibility = VISIBLE;
    private int mId = -1;
    private ViewGroup mParent;
    private OnClickListener mOnClickListener;
    private OnLongClickListener mOnLongClickListener;
    private OnTouchListener mOnTouchListener;
    private OnFocusChangeListener mOnFocusChangeListener;
    private OnKeyListener mOnKeyListener;
    private AccessibilityDelegate mAccessibilityDelegate;
    private ViewTreeObserver mViewTreeObserver;
    private boolean mHasFocus;
    private long mTouchDownTime;
    private boolean mLongClickFired;

    /** Ngưỡng nhấn giữ của Android (ViewConfiguration.getLongPressTimeout). */
    private static final long LONG_PRESS_TIMEOUT = 500L;

    /**
     * giao diện cho các cuộc gọi lại nhấp chuột.
     */
    public interface OnClickListener {
        void onClick(View v);
    }

    /**
     * Callback nhấn giữ. Trả true nếu đã xử lý (chặn onClick).
     */
    public interface OnLongClickListener {
        boolean onLongClick(View v);
    }

    /**
     * Callback touch thô. Trả true để tiêu thụ sự kiện.
     */
    public interface OnTouchListener {
        boolean onTouch(View v, MotionEvent event);
    }

    /**
     * Callback khi view nhận/mất focus.
     */
    public interface OnFocusChangeListener {
        void onFocusChange(View v, boolean hasFocus);
    }

    /**
     * Callback kéo-thả. Trả true nếu đã xử lý DragEvent.
     */
    public interface OnDragListener {
        boolean onDrag(View v, Object event);
    }

    /**
     * Callback phím cứng gửi tới view đang focus.
     */
    public interface OnKeyListener {
        boolean onKey(View v, int keyCode, KeyEvent event);
    }

    /**
     * Callback áp dụng window insets (status bar / notch).
     */
    public interface OnApplyWindowInsetsListener {
        Object onApplyWindowInsets(View v, Object insets);
    }

    /**
     * Callback khi view được attach/detach khỏi window.
     */
    public interface OnAttachStateChangeListener {
        void onViewAttachedToWindow(View v);

        void onViewDetachedFromWindow(View v);
    }

    /**
     * Cung cấp bitmap bóng khi kéo view. Android: lớp abstract lồng trong View.
     */
    public static class DragShadowBuilder {
        private final View mView;

        public DragShadowBuilder(View view) {
            mView = view;
        }

        public DragShadowBuilder() {
            mView = null;
        }

        public View getView() {
            return mView;
        }

        public void onProvideShadowMetrics(android.graphics.Point outShadowSize,
                                          android.graphics.Point outShadowTouchPoint) {
            if (outShadowSize != null && mView != null) {
                outShadowSize.set(Math.max(1, mView.getWidth()), Math.max(1, mView.getHeight()));
            }
            if (outShadowTouchPoint != null && outShadowSize != null) {
                outShadowTouchPoint.set(outShadowSize.x / 2, outShadowSize.y / 2);
            }
        }

        public void onDrawShadow(android.graphics.Canvas canvas) {
            if (mView != null && canvas != null) mView.draw(canvas);
        }
    }

    /**
     * Chặn/điều hướng sự kiện accessibility. Mọi hàm mặc định uỷ quyền về view.
     */
    public static class AccessibilityDelegate {
        public void sendAccessibilityEvent(View host, int eventType) {
        }

        public boolean performAccessibilityAction(View host, int action, Bundle args) {
            return false;
        }
    }

    public View(Context context) {
        mContext = context;
    }

    /**
     * trả về ngữ cảnh mà view này được tạo với.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * trả về id của view.
     */
    public int getId() {
        return mId;
    }

    /**
     * thiết lập id của view.
     */
    public void setId(int id) {
        mId = id;
    }

    public View findViewById(int id) {
        if (mId == id) return this;
        return null;
    }

    /**
     * trả về vị trí bên trái của view.
     */
    public int getLeft() {
        return mLeft;
    }

    /**
     * trả về vị trí trên cùng của view.
     */
    public int getTop() {
        return mTop;
    }

    /**
     * trả về vị trí bên phải của view.
     */
    public int getRight() {
        return mRight;
    }

    /**
     * trả về vị trí dưới cùng của view.
     */
    public int getBottom() {
        return mBottom;
    }

    /**
     * trả về chiều rộng của view.
     */
    public int getWidth() {
        return mRight - mLeft;
    }

    /**
     * trả về chiều cao của view.
     */
    public int getHeight() {
        return mBottom - mTop;
    }

    /**
     * thiết lập ranh giới bố cục của view.
     */
    public void layout(int l, int t, int r, int b) {
        mLeft = l;
        mTop = t;
        mRight = r;
        mBottom = b;
    }

    /**
     * trả về khả năng hiển thị của view.
     */
    public int getVisibility() {
        return mVisibility;
    }

    /**
     * thiết lập khả năng hiển thị của view.
     */
    public void setVisibility(int visibility) {
        mVisibility = visibility;
    }

    /**
     * trả về cha của view.
     */
    public ViewGroup getParent() {
        return mParent;
    }

    /**
     * thiết lập cha của view.
     */
    public void setParent(ViewGroup parent) {
        mParent = parent;
    }

    /**
     * thiết lập trình nghe nhấp chuột.
     */
    public void setOnClickListener(OnClickListener l) {
        mOnClickListener = l;
    }

    public void setOnLongClickListener(OnLongClickListener l) {
        mOnLongClickListener = l;
    }

    public void setOnTouchListener(OnTouchListener l) {
        mOnTouchListener = l;
    }

    public void setOnFocusChangeListener(OnFocusChangeListener l) {
        mOnFocusChangeListener = l;
    }

    public void setOnKeyListener(OnKeyListener l) {
        mOnKeyListener = l;
    }

    public boolean isClickable() {
        return mOnClickListener != null;
    }

    public boolean isLongClickable() {
        return mOnLongClickListener != null;
    }

    public boolean hasFocus() {
        return mHasFocus;
    }

    public boolean isFocused() {
        return mHasFocus;
    }

    /** Đổi trạng thái focus và bắn callback nếu có thay đổi thật. */
    public void setFocus(boolean hasFocus) {
        if (mHasFocus == hasFocus) return;
        mHasFocus = hasFocus;
        if (mOnFocusChangeListener != null) {
            mOnFocusChangeListener.onFocusChange(this, hasFocus);
        }
    }

    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event == null) return false;
        if (mOnKeyListener != null &&
            mOnKeyListener.onKey(this, event.getKeyCode(), event)) {
            return true;
        }
        return onKeyDown(event.getKeyCode(), event);
    }

    public boolean onKeyDown(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean onKeyUp(int keyCode, KeyEvent event) {
        return false;
    }

    /**
     * ViewTreeObserver của cây view này (dùng chung với cha nếu đã attach).
     */
    public ViewTreeObserver getViewTreeObserver() {
        if (mParent != null) return mParent.getViewTreeObserver();
        if (mViewTreeObserver == null) mViewTreeObserver = new ViewTreeObserver();
        return mViewTreeObserver;
    }

    public void setAccessibilityDelegate(AccessibilityDelegate delegate) {
        mAccessibilityDelegate = delegate;
    }

    public boolean dispatchTouchEvent(MotionEvent event) {
        if (mVisibility != VISIBLE) return false;
        // OnTouchListener chạy TRƯỚC onTouchEvent và có quyền tiêu thụ sự kiện —
        // thứ tự này là hợp đồng của Android, app dựa vào nó để chặn click.
        if (mOnTouchListener != null && mOnTouchListener.onTouch(this, event)) {
            return true;
        }
        return onTouchEvent(event);
    }

    public boolean onTouchEvent(MotionEvent event) {
        if (event == null) return false;
        final int action = event.getAction();
        if (action == MotionEvent.ACTION_DOWN) {
            mTouchDownTime = android.os.SystemClock.uptimeMillis();
            mLongClickFired = false;
            return mOnClickListener != null || mOnLongClickListener != null;
        }
        if (action == MotionEvent.ACTION_MOVE) {
            if (!mLongClickFired && mOnLongClickListener != null && mTouchDownTime > 0 &&
                android.os.SystemClock.uptimeMillis() - mTouchDownTime >= LONG_PRESS_TIMEOUT) {
                mLongClickFired = true;
                return performLongClick();
            }
            return mOnClickListener != null || mOnLongClickListener != null;
        }
        if (action == MotionEvent.ACTION_UP) {
            final long held = mTouchDownTime > 0
                    ? android.os.SystemClock.uptimeMillis() - mTouchDownTime : 0;
            mTouchDownTime = 0;
            if (mLongClickFired) return true;
            if (mOnLongClickListener != null && held >= LONG_PRESS_TIMEOUT) {
                return performLongClick();
            }
            if (mOnClickListener != null) {
                performClick();
                return true;
            }
            return mOnLongClickListener != null;
        }
        return mOnClickListener != null || mOnLongClickListener != null;
    }

    /**
     * thực hiện một cú nhấp chuột.
     */
    public boolean performClick() {
        if (mOnClickListener != null) {
            mOnClickListener.onClick(this);
            return true;
        }
        return false;
    }

    public boolean performLongClick() {
        if (mOnLongClickListener != null) {
            return mOnLongClickListener.onLongClick(this);
        }
        return false;
    }

    /**
     * đo view.
     */
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
    }

    /**
     * đo view (được gọi bởi cha).
     */
    public void measure(int widthMeasureSpec, int heightMeasureSpec) {
        onMeasure(widthMeasureSpec, heightMeasureSpec);
    }

    private int mBackgroundColor = 0;

    /**
     * vẽ view.
     */
    protected void onDraw(Canvas canvas) {
    }

    /**
     * vẽ view (được gọi bởi cha).
     */
    public void draw(Canvas canvas) {
        if (mVisibility != VISIBLE || canvas == null) return;
        if (mBackgroundColor != 0) {
            android.graphics.Paint p = new android.graphics.Paint();
            p.setColor(mBackgroundColor);
            canvas.drawRect(mLeft, mTop, mRight, mBottom, p);
        }
        onDraw(canvas);
    }

    /**
     * trả về trạng thái hiển thị của view.
     */
    public boolean isShown() {
        return mVisibility == VISIBLE;
    }

    /**
     * trả về nền của view.
     */
    public android.graphics.drawable.Drawable getBackground() {
        return null;
    }

    /**
     * thiết lập nền của view.
     */
    public void setBackgroundColor(int color) {
        mBackgroundColor = color;
        invalidate();
    }

    /**
     * thiết lập drawable nền của view.
     */
    public void setBackgroundDrawable(android.graphics.drawable.Drawable background) {
    }

    /**
     * thiết lập phần đệm của view.
     */
    public void setPadding(int left, int top, int right, int bottom) {
    }

    /**
     * làm mất hiệu lực view.
     */
    public void invalidate() {
    }

    /**
     * đăng một runnable lên luồng ui.
     */
    public boolean post(Runnable action) {
        action.run();
        return true;
    }

    /**
     * trả về thẻ (tag) của view.
     */
    public Object getTag() {
        return null;
    }

    /**
     * thiết lập thẻ (tag) của view.
     */
    private boolean mKeepScreenOn = false;
    private static native void setKeepScreenOnNative(boolean keepOn);

    public void setKeepScreenOn(boolean keepScreenOn) {
        mKeepScreenOn = keepScreenOn;
        try {
            setKeepScreenOnNative(keepScreenOn);
        } catch (Throwable ignored) {}
    }

    public boolean getKeepScreenOn() {
        return mKeepScreenOn;
    }

    public static class BaseSavedState {
        public BaseSavedState() {}
    }

    public interface OnCreateContextMenuListener {
    }

    public interface OnHoverListener {
    }

    public interface OnLayoutChangeListener {
    }

    public interface OnSystemUiVisibilityChangeListener {
    }

    public interface OnUnhandledKeyEventListener {
    }

}
