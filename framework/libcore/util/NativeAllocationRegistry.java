package libcore.util;

public final class NativeAllocationRegistry {
    public static NativeAllocationRegistry createMalloced(ClassLoader loader, long freeFn) {
        return new NativeAllocationRegistry();
    }
    public static NativeAllocationRegistry createNonmalloced(ClassLoader loader, long freeFn, long size) {
        return new NativeAllocationRegistry();
    }
    public Runnable registerNativeAllocation(Object referent, long nativePtr) {
        return new Runnable() {
            @Override
            public void run() {}
        };
    }
}
