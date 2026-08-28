package java.lang.invoke;

public abstract class CallSite {
    public abstract MethodHandle getTarget();
    public abstract void setTarget(MethodHandle newTarget);
    public abstract MethodType type();
}
