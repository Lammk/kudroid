package android.widget;

/**
 * android.widget.Checkable — view with checked/unchecked state.
 */
public interface Checkable {
    void setChecked(boolean checked);

    boolean isChecked();

    void toggle();
}
