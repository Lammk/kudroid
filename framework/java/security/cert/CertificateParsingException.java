package java.security.cert;

public class CertificateParsingException extends CertificateException {
    private static final long serialVersionUID = -7989222416793322029L;
    public CertificateParsingException() { super(); }
    public CertificateParsingException(String msg) { super(msg); }
    public CertificateParsingException(String msg, Throwable cause) { super(msg, cause); }
}
