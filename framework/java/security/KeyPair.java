package java.security;

import java.io.Serializable;

public final class KeyPair implements Serializable {
    private final PrivateKey privateKey;
    private final PublicKey publicKey;

    public KeyPair(PublicKey publicKey, PrivateKey privateKey) {
        this.publicKey = publicKey;
        this.privateKey = privateKey;
    }
    public PublicKey getPublic() { return publicKey; }
    public PrivateKey getPrivate() { return privateKey; }
}
