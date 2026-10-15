package libcore.net.ssl;

import java.math.BigInteger;
import java.security.Principal;
import java.security.PublicKey;
import java.security.cert.CertificateEncodingException;
import java.security.cert.CertificateException;
import java.security.cert.CertificateExpiredException;
import java.security.cert.CertificateNotYetValidException;
import java.security.cert.CertificateParsingException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import javax.security.auth.x500.X500Principal;

/** An X.509 certificate read from its DER encoding. */
public final class KuX509Certificate extends X509Certificate {
    private static final String OID_SAN = "2.5.29.17";
    private static final String OID_BASIC_CONSTRAINTS = "2.5.29.19";

    private final byte[] der;
    private int version = 1;
    private BigInteger serial;
    private X500Principal issuer;
    private X500Principal subject;
    private long notBefore;
    private long notAfter;
    private String sigAlgOid;
    private byte[] signature;
    private byte[] spki;
    private String keyAlgorithm = "";
    private List<List<?>> subjectAltNames;
    private int basicConstraints = -1;

    public static KuX509Certificate parse(byte[] der) throws CertificateParsingException {
        try {
            return new KuX509Certificate(der);
        } catch (RuntimeException e) {
            throw new CertificateParsingException("Could not parse certificate: " + e.getMessage());
        }
    }

    private KuX509Certificate(byte[] encoded) {
        this.der = encoded.clone();
        Der outer = new Der(der);
        outer.expect(Der.SEQUENCE);
        Der cert = outer.inside();
        cert.expect(Der.SEQUENCE);
        Der tbs = cert.inside();

        // version [0] EXPLICIT INTEGER DEFAULT v1
        int save = tbs.pos;
        tbs.next();
        if (tbs.tag == 0xa0) {
            Der v = tbs.inside();
            v.expect(Der.INTEGER);
            version = (v.data[v.start + v.length - 1] & 0xff) + 1;
        } else {
            tbs.pos = save;
        }
        tbs.expect(Der.INTEGER);
        serial = new BigInteger(tbs.content());
        tbs.expect(Der.SEQUENCE);  // signature algorithm
        int issuerStart = tbs.pos;
        tbs.expect(Der.SEQUENCE);
        issuer = name(tbs, issuerStart);

        tbs.expect(Der.SEQUENCE);  // validity
        Der validity = tbs.inside();
        notBefore = time(validity);
        notAfter = time(validity);

        int subjectStart = tbs.pos;
        tbs.expect(Der.SEQUENCE);
        subject = name(tbs, subjectStart);

        int spkiStart = tbs.pos;
        tbs.expect(Der.SEQUENCE);
        spki = tbs.whole(spkiStart);
        Der keyInfo = tbs.inside();
        keyInfo.expect(Der.SEQUENCE);
        Der alg = keyInfo.inside();
        alg.expect(Der.OID);
        String keyOid = alg.oid();
        keyAlgorithm = keyOid.equals("1.2.840.113549.1.1.1") ? "RSA"
                : keyOid.equals("1.2.840.10045.2.1") ? "EC" : keyOid;

        while (tbs.hasMore()) {
            tbs.next();
            if (tbs.tag == 0xa3) {  // extensions [3]
                extensions(tbs.inside());
            }
        }

        cert.expect(Der.SEQUENCE);  // outer signature algorithm
        Der sigAlg = cert.inside();
        sigAlg.expect(Der.OID);
        sigAlgOid = sigAlg.oid();
        cert.expect(Der.BIT_STRING);
        signature = new byte[Math.max(cert.length - 1, 0)];
        System.arraycopy(cert.data, cert.start + 1, signature, 0, signature.length);
    }

    // ── parsing helpers ──────────────────────────────────────────────────────

    /** Reads a Name whose SEQUENCE header was just consumed; text is RFC 2253 (last RDN first). */
    private X500Principal name(Der parent, int headerStart) {
        Der rdns = parent.inside();
        ArrayList<String> parts = new ArrayList<String>();
        while (rdns.hasMore()) {
            rdns.expect(Der.SET);
            Der set = rdns.inside();
            StringBuilder rdn = new StringBuilder();
            while (set.hasMore()) {
                set.expect(Der.SEQUENCE);
                Der atv = set.inside();
                atv.expect(Der.OID);
                String oid = atv.oid();
                atv.next();
                if (rdn.length() > 0) rdn.append('+');
                rdn.append(attributeName(oid)).append('=').append(escape(atv.string()));
            }
            parts.add(rdn.toString());
        }
        StringBuilder text = new StringBuilder();
        for (int i = parts.size() - 1; i >= 0; i--) {
            if (text.length() > 0) text.append(',');
            text.append(parts.get(i));
        }
        return new X500Principal(text.toString(), parent.whole(headerStart));
    }

    private static String attributeName(String oid) {
        if (oid.equals("2.5.4.3")) return "CN";
        if (oid.equals("2.5.4.6")) return "C";
        if (oid.equals("2.5.4.7")) return "L";
        if (oid.equals("2.5.4.8")) return "ST";
        if (oid.equals("2.5.4.10")) return "O";
        if (oid.equals("2.5.4.11")) return "OU";
        if (oid.equals("2.5.4.5")) return "SERIALNUMBER";
        if (oid.equals("1.2.840.113549.1.9.1")) return "EMAILADDRESS";
        if (oid.equals("0.9.2342.19200300.100.1.25")) return "DC";
        return oid;
    }

    private static String escape(String v) {
        StringBuilder sb = new StringBuilder(v.length());
        for (int i = 0; i < v.length(); i++) {
            char c = v.charAt(i);
            if (c == ',' || c == '+' || c == '"' || c == '\\' || c == '<' || c == '>' || c == ';') {
                sb.append('\\');
            }
            sb.append(c);
        }
        return sb.toString();
    }

    private static long time(Der d) {
        d.next();
        String s = d.string();
        int i = 0;
        int year;
        if (d.tag == Der.UTC_TIME) {
            year = Integer.parseInt(s.substring(0, 2));
            year += year < 50 ? 2000 : 1900;
            i = 2;
        } else {
            year = Integer.parseInt(s.substring(0, 4));
            i = 4;
        }
        int month = Integer.parseInt(s.substring(i, i + 2));
        int day = Integer.parseInt(s.substring(i + 2, i + 4));
        int hour = Integer.parseInt(s.substring(i + 4, i + 6));
        int minute = Integer.parseInt(s.substring(i + 6, i + 8));
        int second = s.length() >= i + 10 && Character.isDigit(s.charAt(i + 8))
                ? Integer.parseInt(s.substring(i + 8, i + 10)) : 0;
        long days = libcore.net.http.HttpDate.daysFromCivilPublic(year, month, day);
        return ((days * 24 + hour) * 60 + minute) * 60000L + second * 1000L;
    }

    private void extensions(Der wrapper) {
        wrapper.expect(Der.SEQUENCE);
        Der list = wrapper.inside();
        while (list.hasMore()) {
            list.expect(Der.SEQUENCE);
            Der ext = list.inside();
            ext.expect(Der.OID);
            String oid = ext.oid();
            ext.next();
            if (ext.tag == 0x01) ext.next();  // critical flag
            if (ext.tag != Der.OCTET_STRING) continue;
            Der value = ext.inside();
            if (oid.equals(OID_SAN)) {
                subjectAltNames = altNames(value);
            } else if (oid.equals(OID_BASIC_CONSTRAINTS)) {
                value.expect(Der.SEQUENCE);
                Der bc = value.inside();
                basicConstraints = Integer.MAX_VALUE;
                boolean isCa = false;
                while (bc.hasMore()) {
                    bc.next();
                    if (bc.tag == 0x01) {
                        isCa = bc.length > 0 && bc.data[bc.start] != 0;
                    } else if (bc.tag == Der.INTEGER && isCa) {
                        basicConstraints = bc.data[bc.start + bc.length - 1] & 0xff;
                    }
                }
                if (!isCa) basicConstraints = -1;
            }
        }
    }

    private static List<List<?>> altNames(Der value) {
        value.expect(Der.SEQUENCE);
        Der names = value.inside();
        ArrayList<List<?>> out = new ArrayList<List<?>>();
        while (names.hasMore()) {
            names.next();
            int type = names.tag & 0x1f;
            Object val = null;
            if (type == 2 || type == 1 || type == 6) {
                val = names.string();  // dNSName, rfc822Name, URI
            } else if (type == 7) {
                byte[] raw = names.content();
                try {
                    val = java.net.InetAddress.getByAddress(raw).getHostAddress();
                } catch (java.net.UnknownHostException e) {
                    continue;
                }
            }
            if (val != null) out.add(Collections.unmodifiableList(Arrays.<Object>asList(Integer.valueOf(type), val)));
        }
        return out;
    }

    // ── X509Certificate ──────────────────────────────────────────────────────

    @Override
    public void checkValidity() throws CertificateExpiredException, CertificateNotYetValidException {
        checkValidity(new Date());
    }

    @Override
    public void checkValidity(Date date) throws CertificateExpiredException, CertificateNotYetValidException {
        long t = date.getTime();
        if (t > notAfter) throw new CertificateExpiredException("certificate expired on " + new Date(notAfter));
        if (t < notBefore) throw new CertificateNotYetValidException("certificate not valid until " + new Date(notBefore));
    }

    @Override public int getVersion() { return version; }
    @Override public BigInteger getSerialNumber() { return serial; }
    @Override public Principal getIssuerDN() { return issuer; }
    @Override public Principal getSubjectDN() { return subject; }
    @Override public X500Principal getIssuerX500Principal() { return issuer; }
    @Override public X500Principal getSubjectX500Principal() { return subject; }
    @Override public Date getNotBefore() { return new Date(notBefore); }
    @Override public Date getNotAfter() { return new Date(notAfter); }
    @Override public String getSigAlgOID() { return sigAlgOid; }

    @Override
    public String getSigAlgName() {
        if (sigAlgOid.equals("1.2.840.113549.1.1.11")) return "SHA256withRSA";
        if (sigAlgOid.equals("1.2.840.113549.1.1.12")) return "SHA384withRSA";
        if (sigAlgOid.equals("1.2.840.113549.1.1.13")) return "SHA512withRSA";
        if (sigAlgOid.equals("1.2.840.113549.1.1.5")) return "SHA1withRSA";
        if (sigAlgOid.equals("1.2.840.10045.4.3.2")) return "SHA256withECDSA";
        if (sigAlgOid.equals("1.2.840.10045.4.3.3")) return "SHA384withECDSA";
        return sigAlgOid;
    }

    @Override public byte[] getSignature() { return signature.clone(); }
    @Override public int getBasicConstraints() { return basicConstraints; }

    @Override
    public Collection<List<?>> getSubjectAlternativeNames() throws CertificateParsingException {
        return subjectAltNames == null ? null : Collections.unmodifiableList(subjectAltNames);
    }

    @Override public byte[] getEncoded() throws CertificateEncodingException { return der.clone(); }

    @Override
    public PublicKey getPublicKey() { return new KuPublicKey(keyAlgorithm, spki); }

    @Override
    public boolean equals(Object o) {
        return o instanceof KuX509Certificate && Arrays.equals(der, ((KuX509Certificate) o).der);
    }

    @Override
    public int hashCode() {
        int h = 1;
        for (int i = 0; i < der.length; i++) h = 31 * h + der[i];
        return h;
    }

    @Override
    public String toString() {
        return "X.509 Certificate: Subject: " + subject.getName() + " Issuer: " + issuer.getName()
                + " Serial: " + serial.toString(16);
    }

    /** A public key kept in its SubjectPublicKeyInfo encoding. */
    private static final class KuPublicKey implements PublicKey {
        private static final long serialVersionUID = 1L;
        private final String algorithm;
        private final byte[] encoded;

        KuPublicKey(String algorithm, byte[] encoded) {
            this.algorithm = algorithm;
            this.encoded = encoded;
        }

        @Override public String getAlgorithm() { return algorithm; }
        @Override public String getFormat() { return "X.509"; }
        @Override public byte[] getEncoded() { return encoded.clone(); }
    }
}
