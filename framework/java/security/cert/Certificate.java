package java.security.cert;

import java.io.Serializable;
import java.security.PublicKey;

public abstract class Certificate implements Serializable {
    private final String type;

    protected Certificate(String type) { this.type = type; }
    public final String getType() { return type; }
    public abstract byte[] getEncoded() throws CertificateEncodingException;
    public abstract PublicKey getPublicKey();
}
