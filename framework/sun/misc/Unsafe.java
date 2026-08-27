package sun.misc;

import java.lang.reflect.Field;

public final class Unsafe {
    private static final Unsafe THE_ONE = new Unsafe();

    private Unsafe() {}

    public static Unsafe getUnsafe() {
        return THE_ONE;
    }

    public static Unsafe getUnsafeInstance() {
        return THE_ONE;
    }

    public native boolean compareAndSwapInt(Object obj, long offset, int expectedValue, int newValue);
    public native boolean compareAndSwapLong(Object obj, long offset, long expectedValue, long newValue);
    public native boolean compareAndSwapObject(Object obj, long offset, Object expectedValue, Object newValue);

    public native int getInt(Object obj, long offset);
    public native void putInt(Object obj, long offset, int newValue);

    public native long getLong(Object obj, long offset);
    public native void putLong(Object obj, long offset, long newValue);

    public native Object getObject(Object obj, long offset);
    public native void putObject(Object obj, long offset, Object newValue);

    public native int getIntVolatile(Object obj, long offset);
    public native void putIntVolatile(Object obj, long offset, int newValue);

    public native long getLongVolatile(Object obj, long offset);
    public native void putLongVolatile(Object obj, long offset, long newValue);

    public native Object getObjectVolatile(Object obj, long offset);
    public native void putObjectVolatile(Object obj, long offset, Object newValue);

    public native void putOrderedInt(Object obj, long offset, int newValue);
    public native void putOrderedLong(Object obj, long offset, long newValue);
    public native void putOrderedObject(Object obj, long offset, Object newValue);

    public native long objectFieldOffset(Field field);

    public native int arrayBaseOffset(Class<?> clazz);
    public native int arrayIndexScale(Class<?> clazz);

    public native void park(boolean isAbsolute, long time);
    public native void unpark(Object thread);
}
