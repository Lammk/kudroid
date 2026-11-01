package android.os;

public class StrictMode {
    private static volatile VmPolicy vmPolicy = VmPolicy.LAX;
    public StrictMode() {}

    public static VmPolicy getVmPolicy() { return vmPolicy; }

    public static void setVmPolicy(VmPolicy policy) {
        if (policy == null) throw new NullPointerException("policy == null");
        vmPolicy = policy;
    }

    public static class VmPolicy {
        public static final VmPolicy LAX = new VmPolicy();
        public VmPolicy() {}
    }

    public static class Builder {
        public Builder() {}
    }
}
