package android.widget;

/**
 * android.widget.Checkable — view có trạng thái đã chọn/chưa chọn.
 */
public interface Checkable {
    void setChecked(boolean checked);

    boolean isChecked();

    void toggle();
}
