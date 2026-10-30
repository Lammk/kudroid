package android.view;

import android.graphics.Insets;
import android.graphics.Rect;

/** Immutable per-type window occlusion and visibility. */
public final class WindowInsets {
    public static final WindowInsets CONSUMED = new WindowInsets();
    private final Insets[] mInsets = new Insets[8];
    private final Insets[] mStable = new Insets[8];
    private final boolean[] mVisible = new boolean[8];
    private boolean mSystemConsumed = true, mStableConsumed = true;
    private DisplayCutout mCutout;

    public WindowInsets() {}
    public WindowInsets(WindowInsets src) {
        System.arraycopy(src.mInsets, 0, mInsets, 0, 8);
        System.arraycopy(src.mStable, 0, mStable, 0, 8);
        System.arraycopy(src.mVisible, 0, mVisible, 0, 8);
        mSystemConsumed = src.mSystemConsumed;
        mStableConsumed = src.mStableConsumed;
        mCutout = src.mCutout;
    }
    public WindowInsets(Rect insets) {
        this(new Builder().setSystemWindowInsets(Insets.of(insets)).build());
    }
    private static Insets combine(Insets[] values, int mask) {
        int left = 0, top = 0, right = 0, bottom = 0;
        for (int i = 0; i < 8; i++) if ((mask & (1 << i)) != 0 && values[i] != null) {
            Insets value = values[i];
            left = Math.max(left, value.left); top = Math.max(top, value.top);
            right = Math.max(right, value.right); bottom = Math.max(bottom, value.bottom);
        }
        return Insets.of(left, top, right, bottom);
    }
    public Insets getInsets(int mask) { return combine(mInsets, mask); }
    public Insets getInsetsIgnoringVisibility(int mask) {
        if ((mask & Type.ime()) != 0) throw new IllegalArgumentException("IME maximum insets are unknown");
        return combine(mStable, mask);
    }
    public boolean isVisible(int mask) {
        for (int i = 0; i < 8; i++) if ((mask & (1 << i)) != 0 && !mVisible[i]) return false;
        return true;
    }
    private Insets system() { return mSystemConsumed ? Insets.NONE : getInsets(Type.systemBars() | Type.ime()); }
    public int getSystemWindowInsetLeft() { return system().left; }
    public int getSystemWindowInsetTop() { return system().top; }
    public int getSystemWindowInsetRight() { return system().right; }
    public int getSystemWindowInsetBottom() { return system().bottom; }
    public Insets getSystemWindowInsets() { return system(); }
    public Insets getStableInsets() { return mStableConsumed ? Insets.NONE : combine(mStable, Type.systemBars()); }
    public int getStableInsetLeft() { return getStableInsets().left; }
    public int getStableInsetTop() { return getStableInsets().top; }
    public int getStableInsetRight() { return getStableInsets().right; }
    public int getStableInsetBottom() { return getStableInsets().bottom; }
    public Insets getSystemGestureInsets() { return getInsets(Type.systemGestures()); }
    public Insets getMandatorySystemGestureInsets() { return getInsets(Type.mandatorySystemGestures()); }
    public Insets getTappableElementInsets() { return getInsets(Type.tappableElement()); }
    public boolean hasSystemWindowInsets() { return !system().equals(Insets.NONE); }
    public boolean hasStableInsets() { return !getStableInsets().equals(Insets.NONE); }
    public boolean hasInsets() { return !getInsets(Type.all()).equals(Insets.NONE) || hasStableInsets() || mCutout != null; }
    public boolean isConsumed() { return mSystemConsumed && mStableConsumed && mCutout == null; }
    public WindowInsets consumeSystemWindowInsets() {
        WindowInsets result = new WindowInsets(this);
        result.mSystemConsumed = true;
        for (int i = 0; i < 8; i++) result.mInsets[i] = null;
        return result;
    }
    public WindowInsets consumeStableInsets() {
        WindowInsets result = new WindowInsets(this);
        result.mStableConsumed = true;
        for (int i = 0; i < 8; i++) result.mStable[i] = null;
        return result;
    }
    public WindowInsets consumeDisplayCutout() {
        WindowInsets result = new WindowInsets(this); result.mCutout = null; return result;
    }
    public DisplayCutout getDisplayCutout() { return mCutout; }
    public boolean isRound() { return false; }
    public WindowInsets replaceSystemWindowInsets(int left, int top, int right, int bottom) {
        return new Builder(this).setSystemWindowInsets(Insets.of(left, top, right, bottom)).build();
    }
    public WindowInsets replaceSystemWindowInsets(Rect insets) {
        return replaceSystemWindowInsets(insets.left, insets.top, insets.right, insets.bottom);
    }
    public WindowInsets inset(Rect insets) { return inset(insets.left, insets.top, insets.right, insets.bottom); }
    public WindowInsets inset(int left, int top, int right, int bottom) {
        if (left < 0 || top < 0 || right < 0 || bottom < 0) throw new IllegalArgumentException("negative inset");
        WindowInsets result = new WindowInsets(this);
        for (int i = 0; i < 8; i++) {
            result.mInsets[i] = insetValue(mInsets[i], left, top, right, bottom);
            result.mStable[i] = insetValue(mStable[i], left, top, right, bottom);
        }
        return result;
    }
    private static Insets insetValue(Insets value, int left, int top, int right, int bottom) {
        return value == null ? null : Insets.of(Math.max(0, value.left - left), Math.max(0, value.top - top),
                Math.max(0, value.right - right), Math.max(0, value.bottom - bottom));
    }
    public static final class Type {
        private Type() {}
        public static int statusBars() { return 1; }
        public static int navigationBars() { return 2; }
        public static int captionBar() { return 4; }
        public static int ime() { return 8; }
        public static int systemGestures() { return 16; }
        public static int mandatorySystemGestures() { return 32; }
        public static int tappableElement() { return 64; }
        public static int displayCutout() { return 128; }
        public static int systemBars() { return 7; }
        public static int all() { return -1; }
    }
    public static final class Builder {
        private final WindowInsets mValue;
        public Builder() { mValue = new WindowInsets(); }
        public Builder(WindowInsets insets) { mValue = new WindowInsets(insets); }
        public WindowInsets build() { return new WindowInsets(mValue); }
        public Builder setInsets(int mask, Insets insets) {
            if (insets == null) throw new NullPointerException("insets");
            for (int i = 0; i < 8; i++) if ((mask & (1 << i)) != 0) mValue.mInsets[i] = insets;
            mValue.mSystemConsumed = false; return this;
        }
        public Builder setInsetsIgnoringVisibility(int mask, Insets insets) {
            if ((mask & Type.ime()) != 0) throw new IllegalArgumentException("IME maximum insets are unknown");
            if (insets == null) throw new NullPointerException("insets");
            for (int i = 0; i < 8; i++) if ((mask & (1 << i)) != 0) mValue.mStable[i] = insets;
            mValue.mStableConsumed = false; return this;
        }
        public Builder setVisible(int mask, boolean visible) {
            for (int i = 0; i < 8; i++) if ((mask & (1 << i)) != 0) mValue.mVisible[i] = visible;
            return this;
        }
        public Builder setSystemWindowInsets(Insets insets) {
            setInsets(Type.statusBars(), Insets.of(0, insets.top, 0, 0));
            setInsets(Type.navigationBars(), Insets.of(insets.left, 0, insets.right, insets.bottom));
            return this;
        }
        public Builder setStableInsets(Insets insets) {
            setInsetsIgnoringVisibility(Type.statusBars(), Insets.of(0, insets.top, 0, 0));
            setInsetsIgnoringVisibility(Type.navigationBars(), Insets.of(insets.left, 0, insets.right, insets.bottom));
            return this;
        }
        public Builder setSystemGestureInsets(Insets insets) { return setInsets(Type.systemGestures(), insets); }
        public Builder setMandatorySystemGestureInsets(Insets insets) { return setInsets(Type.mandatorySystemGestures(), insets); }
        public Builder setTappableElementInsets(Insets insets) { return setInsets(Type.tappableElement(), insets); }
        public Builder setDisplayCutout(DisplayCutout cutout) { mValue.mCutout = cutout; return this; }
    }
}
