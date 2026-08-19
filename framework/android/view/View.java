package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;

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

    /**
     * giao diện cho các cuộc gọi lại nhấp chuột.
     */
    public interface OnClickListener {
        void onClick(View v);
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
    public void setTag(Object tag) {
    }
}
