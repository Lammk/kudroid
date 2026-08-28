package java.security.cert;

public class CertificateNotYetValidException extends CertificateException {
    private static final long serialVersionUID = 4355919900041064702L;
    public CertificateNotYetValidException() { super(); }
    public CertificateNotYetValidException(String message) { super(message); }
}
