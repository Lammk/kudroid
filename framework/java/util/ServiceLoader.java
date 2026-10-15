package java.util;

public final class ServiceLoader<S> implements Iterable<S> {
    private final Class<S> service;

    private ServiceLoader(Class<S> svc) {
        service = svc;
    }
    public void reload() {}
    public Iterator<S> iterator() {
        return Collections.<S>emptyList().iterator();
    }
    public static <S> ServiceLoader<S> load(Class<S> service) {
        return new ServiceLoader<S>(service);
    }
    public static <S> ServiceLoader<S> load(Class<S> service, ClassLoader loader) {
        return new ServiceLoader<S>(service);
    }
    public static <S> ServiceLoader<S> loadInstalled(Class<S> service) {
        return new ServiceLoader<S>(service);
    }
}
