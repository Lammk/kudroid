package com.android.internal.util;

public final class Preconditions {
    public static <T> T checkNotNull(T reference) {
        if (reference == null) throw new NullPointerException();
        return reference;
    }
    public static <T> T checkNotNull(T reference, Object errorMessage) {
        if (reference == null) throw new NullPointerException(String.valueOf(errorMessage));
        return reference;
    }
    public static void checkArgument(boolean expression) {
        if (!expression) throw new IllegalArgumentException();
    }
    public static void checkArgument(boolean expression, Object errorMessage) {
        if (!expression) throw new IllegalArgumentException(String.valueOf(errorMessage));
    }
    public static void checkState(boolean expression) {
        if (!expression) throw new IllegalStateException();
    }
    public static void checkState(boolean expression, Object errorMessage) {
        if (!expression) throw new IllegalStateException(String.valueOf(errorMessage));
    }
    public static float checkArgumentFinite(float value, String valueName) {
        if (Float.isNaN(value)) throw new IllegalArgumentException(valueName + " must not be NaN");
        if (Float.isInfinite(value)) throw new IllegalArgumentException(valueName + " must not be infinite");
        return value;
    }
    public static double checkArgumentFinite(double value, String valueName) {
        if (Double.isNaN(value)) throw new IllegalArgumentException(valueName + " must not be NaN");
        if (Double.isInfinite(value)) throw new IllegalArgumentException(valueName + " must not be infinite");
        return value;
    }
}
