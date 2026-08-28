package java.security;

import java.util.ArrayList;
import java.util.List;

/**
 * java.security.Security.
 *
 * The provider list used to start empty, and that is not a state real Java ever has:
 * every JVM and every Android build ships at least one provider. Library code relies
 * on it — {@code Security.getProviders()[0]} appears verbatim in okhttp's platform
 * detection — so an empty list produced an ArrayIndexOutOfBoundsException inside a
 * class initialiser, which marks that class failed for the whole process. On okhttp
 * that means every HTTP call afterwards throws NoClassDefFoundError.
 *
 * A default provider is therefore installed at class-initialisation time. It is
 * named the way Android names its default ("AndroidOpenSSL") because that is what
 * platform-detection code compares against; the algorithms it advertises are the
 * ones KuDroid's MessageDigest actually implements, so nothing is claimed that
 * cannot be delivered.
 */
public final class Security {

    private static final List<Provider> providers = new ArrayList<Provider>();

    static {
        providers.add(new AndroidOpenSSLProvider());
    }

    private Security() {}

    /**
     * The default provider.
     *
     * Not a nested class of a public API type on purpose: apps look providers up by
     * name, never by type, and keeping it package-private avoids advertising a class
     * Android does not have.
     */
    static final class AndroidOpenSSLProvider extends Provider {
        AndroidOpenSSLProvider() {
            super("AndroidOpenSSL", 1.0, "KuDroid default provider");
            // Only what java.security.MessageDigest can really do here. Claiming
            // ciphers KuDroid does not implement would turn a clean
            // NoSuchAlgorithmException into a wrong result.
            put("MessageDigest.MD5", "java.security.MessageDigest");
            put("MessageDigest.SHA-1", "java.security.MessageDigest");
            put("MessageDigest.SHA-256", "java.security.MessageDigest");
            put("SecureRandom.SHA1PRNG", "java.security.SecureRandom");
        }
    }

    public static String getProperty(String key) { return null; }

    public static void setProperty(String key, String datum) {}

    public static int addProvider(Provider provider) {
        if (provider == null) return -1;
        providers.add(provider);
        return providers.size();
    }

    /**
     * Insert at a 1-based position, as the JDK specifies.
     *
     * Order is the point of this method: a library installing Conscrypt at
     * position 1 expects to find it at getProviders()[0] afterwards. Appending
     * regardless — which this used to do — silently defeats that.
     */
    public static int insertProviderAt(Provider provider, int position) {
        if (provider == null) return -1;
        if (getProvider(provider.getName()) != null) return -1;
        if (position < 1 || position > providers.size() + 1) {
            providers.add(provider);
            return providers.size();
        }
        providers.add(position - 1, provider);
        return position;
    }

    public static void removeProvider(String name) {
        if (name == null) return;
        for (int i = 0; i < providers.size(); i++) {
            if (name.equals(providers.get(i).getName())) {
                providers.remove(i);
                return;
            }
        }
    }

    public static Provider[] getProviders() {
        return providers.toArray(new Provider[providers.size()]);
    }

    public static Provider getProvider(String name) {
        if (name == null) return null;
        for (Provider p : providers) {
            if (name.equals(p.getName())) return p;
        }
        return null;
    }

    /**
     * Providers matching a "Service.Algorithm" filter.
     *
     * Returns null when nothing matches, which is what the JDK does and what callers
     * branch on; an empty array would read as "matched, but nothing there".
     */
    public static Provider[] getProviders(String filter) {
        if (filter == null || filter.isEmpty()) return getProviders();
        final List<Provider> out = new ArrayList<Provider>();
        for (Provider p : providers) {
            if (p.getProperty(filter) != null) out.add(p);
        }
        if (out.isEmpty()) return null;
        return out.toArray(new Provider[out.size()]);
    }

    public static String[] getAlgorithms(String serviceName) {
        if (serviceName == null) return new String[0];
        final String prefix = serviceName + ".";
        final List<String> out = new ArrayList<String>();
        for (Provider p : providers) {
            for (Object key : p.keySet()) {
                final String k = String.valueOf(key);
                if (k.startsWith(prefix)) {
                    final String algorithm = k.substring(prefix.length());
                    if (!out.contains(algorithm)) out.add(algorithm);
                }
            }
        }
        return out.toArray(new String[out.size()]);
    }
}
