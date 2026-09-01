package java.lang.reflect;

import java.io.Serializable;

/**
 * Dynamic proxies.
 *
 * The interesting work is native. Creating a proxy means synthesising a class that
 * implements the requested interfaces, which needs a class writer KuART does not
 * have — so DexClassLinker::GetOrCreateProxyClass builds a bodyless class carrying
 * those interfaces instead, and the interpreter forwards any call landing on one of
 * its methods to this object's InvocationHandler.
 *
 * This used to be pure Java, with newProxyInstance returning `new Proxy(h)`. That
 * object does not implement the requested interface and has no method bodies, so a
 * cast to the interface failed, and where the DEX had no cast the first interface
 * call died with "AbstractMethodError: method without body".
 *
 * `h` is read by native code (Interpreter::InvokeProxyMethod) by field name, so
 * renaming it would break dispatch silently.
 */
public class Proxy implements Serializable {
    private static final long serialVersionUID = -2222568056686623797L;

    protected InvocationHandler h;

    protected Proxy() {}

    protected Proxy(InvocationHandler h) {
        this.h = h;
    }

    public static native Object newProxyInstance(ClassLoader loader, Class<?>[] interfaces,
                                                 InvocationHandler h);

    public static native boolean isProxyClass(Class<?> cl);

    public static native InvocationHandler getInvocationHandler(Object proxy);

    public static native Class<?> getProxyClass(ClassLoader loader, Class<?>... interfaces);
}
