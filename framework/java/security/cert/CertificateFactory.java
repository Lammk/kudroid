package java.security.cert;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collection;
import libcore.net.ssl.KuX509Certificate;

/** Builds certificates from their DER or PEM encodings. Only X.509 is offered. */
public class CertificateFactory {
    private static final String BEGIN = "-----BEGIN CERTIFICATE-----";
    private static final String END = "-----END CERTIFICATE-----";

    private final String type;

    protected CertificateFactory(String type) { this.type = type; }

    public static final CertificateFactory getInstance(String type) throws CertificateException {
        if (type == null) throw new NullPointerException("type == null");
        if (!type.equalsIgnoreCase("X.509") && !type.equalsIgnoreCase("X509")) {
            throw new CertificateException("Unsupported certificate type: " + type);
        }
        return new CertificateFactory("X.509");
    }

    public static final CertificateFactory getInstance(String type, String provider) throws CertificateException {
        return getInstance(type);
    }

    public final String getType() { return type; }

    public final Certificate generateCertificate(InputStream in) throws CertificateException {
        Collection<? extends Certificate> all = generateCertificates(in);
        if (all.isEmpty()) throw new CertificateException("Could not parse certificate: no certificate found");
        return all.iterator().next();
    }

    public final Collection<? extends Certificate> generateCertificates(InputStream in)
            throws CertificateException {
        byte[] data = readAll(in);
        ArrayList<Certificate> out = new ArrayList<Certificate>();
        String text = new String(data);
        if (text.indexOf(BEGIN) >= 0) {
            int from = 0;
            for (;;) {
                int b = text.indexOf(BEGIN, from);
                if (b < 0) break;
                int e = text.indexOf(END, b);
                if (e < 0) throw new CertificateException("Could not parse certificate: unterminated PEM block");
                String body = text.substring(b + BEGIN.length(), e).trim();
                out.add(KuX509Certificate.parse(java.util.Base64.getMimeDecoder().decode(body)));
                from = e + END.length();
            }
        } else if (data.length > 0) {
            out.add(KuX509Certificate.parse(data));
        }
        return out;
    }

    private static byte[] readAll(InputStream in) throws CertificateException {
        if (in == null) throw new CertificateException("Could not parse certificate: null stream");
        try {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf, 0, buf.length)) >= 0) out.write(buf, 0, n);
            return out.toByteArray();
        } catch (IOException e) {
            throw new CertificateException("Could not read certificate", e);
        }
    }
}
