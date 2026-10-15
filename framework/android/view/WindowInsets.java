package android.view;

import android.graphics.Rect;

public final class WindowInsets {
    public static final WindowInsets CONSUMED = new WindowInsets();

    public WindowInsets() {}
    public WindowInsets(WindowInsets src) {}
    public int getSystemWindowInsetLeft() { return 0; }
    public int getSystemWindowInsetTop() { return 0; }
    public int getSystemWindowInsetRight() { return 0; }
    public int getSystemWindowInsetBottom() { return 0; }
    public boolean hasSystemWindowInsets() { return false; }
    public boolean hasInsets() { return false; }
    public boolean isConsumed() { return true; }
    public WindowInsets consumeSystemWindowInsets() { return this; }
    public DisplayCutout getDisplayCutout() { return null; }

    public static final class Builder {
        public Builder() {}
        public Builder(WindowInsets insets) {}
        public WindowInsets build() { return new WindowInsets(); }
    }
}
