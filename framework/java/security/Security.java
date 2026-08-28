package java.security;

import java.util.ArrayList;
import java.util.List;

public final class Security {
    private static final List<Provider> providers = new ArrayList<Provider>();

    private Security() {}

    public static String getProperty(String key) { return null; }
    public static void setProperty(String key, String datum) {}
    public static int addProvider(Provider provider) {
        if (provider != null) {
            providers.add(provider);
            return providers.size();
        }
        return -1;
    }
    public static int insertProviderAt(Provider provider, int position) {
        return addProvider(provider);
    }
    public static void removeProvider(String name) {}
    public static Provider[] getProviders() {
        return providers.toArray(new Provider[providers.size()]);
    }
    public static Provider getProvider(String name) {
        for (Provider p : providers) {
            if (p.getName().equals(name)) return p;
        }
        return null;
    }
}
