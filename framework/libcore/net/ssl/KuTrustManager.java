package libcore.net.ssl;

import java.io.ByteArrayOutputStream;
import java.security.cert.CertificateEncodingException;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.List;
import javax.net.ssl.X509TrustManager;
import libcore.io.KuTls;

/** Trusts a server chain when the platform can build it to a trusted root. */
public final class KuTrustManager implements X509TrustManager {
    private static final KuTrustManager PLATFORM = new KuTrustManager(null);
    private static final X509Certificate[] NONE = new X509Certificate[0];

    private final byte[] anchors;
    private final X509Certificate[] anchorCerts;

    /** The manager backed by the platform's roots. */
    public static KuTrustManager platform() { return PLATFORM; }

    /** A manager that trusts only the given roots. */
    public static KuTrustManager forAnchors(List<X509Certificate> roots) throws CertificateException {
        return new KuTrustManager(roots.toArray(new X509Certificate[roots.size()]));
    }

    private KuTrustManager(X509Certificate[] roots) {
        this.anchorCerts = roots;
        byte[] packed = null;
        if (roots != null) {
            try {
                packed = pack(roots);
            } catch (CertificateEncodingException e) {
                packed = new byte[0];
            }
        }
        this.anchors = packed;
    }

    private static byte[] pack(X509Certificate[] chain) throws CertificateEncodingException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (int i = 0; i < chain.length; i++) {
            byte[] der = chain[i].getEncoded();
            out.write((der.length >>> 24) & 0xff);
            out.write((der.length >>> 16) & 0xff);
            out.write((der.length >>> 8) & 0xff);
            out.write(der.length & 0xff);
            out.write(der, 0, der.length);
        }
        return out.toByteArray();
    }

    @Override
    public void checkServerTrusted(X509Certificate[] chain, String authType) throws CertificateException {
        if (chain == null || chain.length == 0) throw new IllegalArgumentException("null or zero-length certificate chain");
        if (authType == null || authType.length() == 0) throw new IllegalArgumentException("null or zero-length authentication type");
        byte[] packed = pack(chain);
        byte[] err = new byte[512];
        int rc = KuTls.evaluate(packed, packed.length, anchors, anchors == null ? 0 : anchors.length, err);
        if (rc != 0) {
            int n = Math.min(rc - 1, err.length);
            String reason = n > 0 ? new String(err, 0, n) : "unable to verify the certificate chain";
            throw new CertificateException("Trust anchor for certification path not found: " + reason);
        }
    }

    @Override
    public void checkClientTrusted(X509Certificate[] chain, String authType) throws CertificateException {
        throw new CertificateException("client certificate validation is not supported");
    }

    @Override
    public X509Certificate[] getAcceptedIssuers() {
        return anchorCerts != null ? anchorCerts.clone() : NONE;
    }
}
