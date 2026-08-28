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
}
