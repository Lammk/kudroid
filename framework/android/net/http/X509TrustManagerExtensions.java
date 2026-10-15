package android.net.http;

import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.List;
import javax.net.ssl.SSLSession;
import javax.net.ssl.X509TrustManager;

/**
 * Android's extension over {@link X509TrustManager}, used for certificate pinning.
 *
 * HTTP stacks construct one of these to reach {@code checkServerTrusted} with a
 * hostname, which plain JSSE does not offer. Both okhttp and Microsoft's
 * libHttpClient do it during setup — and because it happens at setup, a failure here
 * disables TLS verification bookkeeping for the whole session rather than one request.
 *
 * The auto-generated stub this replaces had only a no-arg constructor, so
 * {@code new X509TrustManagerExtensions(tm)} resolved to nothing, returned null and
 * left the caller holding an object whose methods all threw. The log line was
 * "Auto-stubbing missing framework method:
 * Landroid/net/http/X509TrustManagerExtensions;-><init>(Ljavax/net/ssl/X509TrustManager;)V".
 *
 * Verification delegates to the wrapped trust manager. KuDroid does not carry its own
 * trust store, so what the platform manager decides is the answer; this class only
 * adds the hostname-aware entry point and the chain accessor around it.
 */
public class X509TrustManagerExtensions {

    private final X509TrustManager mDelegate;

    /**
     * @throws IllegalArgumentException if {@code tm} is null, matching Android — the
     *     constructor there requires a manager that implements the internal
     *     TrustManagerImpl and rejects anything else, and a caller that passes null
     *     has a bug worth reporting at the point it happens rather than on first use.
     */
    public X509TrustManagerExtensions(X509TrustManager tm) throws IllegalArgumentException {
        if (tm == null) {
            throw new IllegalArgumentException("tm is null");
        }
        mDelegate = tm;
    }

    /**
     * Verify the chain and return it, ordered leaf-first.
     *
     * `host` is accepted and ignored: the wrapped manager decides trust, and KuDroid
     * has no separate per-host pinning configuration to consult. Returning the chain
     * the caller supplied is what pinning code needs — it compares public keys in the
     * result against its own pin set, so an empty list would silently pass or fail
     * every pin depending on which way the caller tests it.
     */
    public List<X509Certificate> checkServerTrusted(X509Certificate[] chain, String authType,
            String host) throws CertificateException {
        if (chain == null || chain.length == 0) {
            throw new CertificateException("chain is empty");
        }
        mDelegate.checkServerTrusted(chain, authType);
        List<X509Certificate> result = new ArrayList<X509Certificate>();
        for (int i = 0; i < chain.length; i++) {
            if (chain[i] != null) result.add(chain[i]);
        }
        return result;
    }

    /**
     * Whether the chain was issued by a user-installed CA.
     *
     * Always false: KuDroid has no user CA store, so nothing can have come from one.
     * Apps use this to decide whether to relax pinning for debugging proxies, and
     * answering true would relax it for every connection.
     */
    public boolean isUserAddedCertificate(X509Certificate cert) {
        return false;
    }

    /** Whether this platform supports certificate transparency verification. */
    public boolean isCTVerificationRequired(String hostname) {
        return false;
    }

    public void setCTEnabledOverride(boolean enabled) {
    }

    /**
     * Sockets carrying an SSLSession get the same treatment as the array form; the
     * session is not consulted for the same reason `host` is not.
     */
    public List<X509Certificate> checkServerTrusted(X509Certificate[] chain, String authType,
            SSLSession session) throws CertificateException {
        return checkServerTrusted(chain, authType, (String) null);
    }
}
