package java.security.cert;

import java.math.BigInteger;
import java.security.Principal;
import java.security.PublicKey;
import java.util.Date;

public abstract class X509Certificate extends Certificate {
    protected X509Certificate() { super("X.509"); }
    public abstract void checkValidity() throws CertificateExpiredException, CertificateNotYetValidException;
    public abstract void checkValidity(Date date) throws CertificateExpiredException, CertificateNotYetValidException;
    public abstract int getVersion();
    public abstract BigInteger getSerialNumber();
    public abstract Principal getIssuerDN();
    public abstract Principal getSubjectDN();
    public abstract Date getNotBefore();
    public abstract Date getNotAfter();
    public abstract String getSigAlgName();
    public abstract byte[] getSignature();

    public javax.security.auth.x500.X500Principal getSubjectX500Principal() {
        return new javax.security.auth.x500.X500Principal(getSubjectDN().getName());
    }

    public javax.security.auth.x500.X500Principal getIssuerX500Principal() {
        return new javax.security.auth.x500.X500Principal(getIssuerDN().getName());
    }

    public String getSigAlgOID() { return null; }

    /** The path length constraint when this is a CA certificate, else -1. */
    public int getBasicConstraints() { return -1; }

    public boolean[] getKeyUsage() { return null; }

    public java.util.List<String> getExtendedKeyUsage() throws CertificateParsingException { return null; }

    /** Pairs of {type, value} (2 = DNS name, 7 = IP address), or null when there is no SAN. */
    public java.util.Collection<java.util.List<?>> getSubjectAlternativeNames()
            throws CertificateParsingException {
        return null;
    }

    public java.util.Collection<java.util.List<?>> getIssuerAlternativeNames()
            throws CertificateParsingException {
        return null;
    }
}
