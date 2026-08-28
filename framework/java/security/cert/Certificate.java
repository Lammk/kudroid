package java.security.cert;

import java.io.Serializable;
import java.security.PublicKey;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.InvalidKeyException;
import java.security.SignatureException;

public abstract class Certificate implements Serializable {
    private static final long serialVersionUID = -3585440601605666277L;
    private final String type;

    protected Certificate(String type) {
        this.type = type;
    }
    public final String getType() { return type; }
    public abstract byte[] getEncoded() throws CertificateEncodingException;
    public abstract void verify(PublicKey key) throws CertificateException, NoSuchAlgorithmException, InvalidKeyException, NoSuchProviderException, SignatureException;
    public abstract void verify(PublicKey key, String sigProvider) throws CertificateException, NoSuchAlgorithmException, InvalidKeyException, NoSuchProviderException, SignatureException;
    public abstract String toString();
    public abstract PublicKey getPublicKey();
}
