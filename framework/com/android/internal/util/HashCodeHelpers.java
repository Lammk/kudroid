package com.android.internal.util;

public final class HashCodeHelpers {
    public static int hashCode(int... values) {
        int hash = 1;
        for (int v : values) hash = 31 * hash + v;
        return hash;
    }
    public static int hashCodeGeneric(Object... values) {
        int hash = 1;
        for (Object v : values) hash = 31 * hash + (v != null ? v.hashCode() : 0);
        return hash;
    }
}
