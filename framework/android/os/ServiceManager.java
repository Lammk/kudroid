package android.os;

/**
 * Service registry handle.
 *
 * KuDroid has no binder service daemon; the framework provides every system
 * service the guest can reach as an in-process object. Guests that look a
 * service up by name through here get null for names the framework does not
 * carry, which is the same observable answer a name without a service manager
 * entry would give — and callers already handle a null binder (they must, to
 * run before the service is published).
 */
public final class ServiceManager {
    private ServiceManager() {
    }

    /**
     * Returns a binder for the named service, or null when KuDroid does not
     * implement it. Never throws: a guest probing for an optional service
     * relies on the null return rather than an exception.
     */
    public static IBinder getService(String name) {
        return null;
    }

    /**
     * Publish a service under a name. In-process only; a later put for the
     * same name replaces the earlier binder.
     */
    public static void addService(String name, IBinder binder) {
    }

    /** List every published service name. Empty here: nothing is pre-registered. */
    public static String[] listServices() {
        return new String[0];
    }

    /**
     * Like getService but for services that live in this process. KuDroid has
     * none, so this is the same null answer as getService.
     */
    public static IBinder checkService(String name) {
        return null;
    }
}
