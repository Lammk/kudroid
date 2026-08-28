package java.security.cert;

public class CertificateExpiredException extends CertificateException {
    private static final long serialVersionUID = 9075041339151533798L;
    public CertificateExpiredException() { super(); }
    public CertificateExpiredException(String message) { super(message); }
}
