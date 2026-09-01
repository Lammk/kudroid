package java.lang.reflect;

import java.io.Serializable;

public class Proxy implements Serializable {
    private static final long serialVersionUID = -2222568056686623797L;

    protected InvocationHandler h;

    protected Proxy() {}

    protected Proxy(InvocationHandler h) {
        this.h = h;
    }

    public static Object newProxyInstance(ClassLoader loader, Class<?>[] interfaces, InvocationHandler h) {
        return new Proxy(h);
    }

    public static boolean isProxyClass(Class<?> cl) {
        return cl != null && Proxy.class.isAssignableFrom(cl);
    }

    public static InvocationHandler getInvocationHandler(Object proxy) {
        if (proxy instanceof Proxy) {
            return ((Proxy) proxy).h;
        }
        throw new IllegalArgumentException("not a proxy instance");
    }

    public static Class<?> getProxyClass(ClassLoader loader, Class<?>... interfaces) {
        return Proxy.class;
    }
}
