import java.security.*;
import java.security.spec.*;
import java.util.Arrays;

public class SecurityStartupTests {
    private static int checks;
    private static void check(boolean value, String message) {
        checks++;
        if (!value) throw new AssertionError(message);
    }
    public static int run() throws Exception {
        checks = 0;
        byte[] encoded = Arrays.copyOf(RsaTestVectors.KEY, RsaTestVectors.KEY.length);
        X509EncodedKeySpec spec = new X509EncodedKeySpec(encoded);
        encoded[0] = 0;
        check(spec.getEncoded()[0] == 0x30, "input encoding is copied");
        byte[] copy = spec.getEncoded(); copy[0] = 0;
        check(spec.getEncoded()[0] == 0x30, "key specification encoding is copied");
        check("X.509".equals(spec.getFormat()), "key specification format");
        KeyFactory factory = KeyFactory.getInstance("rsa");
        PublicKey key = factory.generatePublic(spec);
        check("rsa".equals(factory.getAlgorithm()), "factory preserves algorithm name");
        check("RSA".equals(key.getAlgorithm()) && "X.509".equals(key.getFormat()), "public key metadata");
        copy = key.getEncoded(); copy[0] = 0;
        check(key.getEncoded()[0] == 0x30, "public key encoding is immutable");
        String[] algorithms = {"SHA224withRSA", "SHA256withRSA", "SHA384withRSA", "SHA512withRSA"};
        byte[][] signatures = {RsaTestVectors.SHA224, RsaTestVectors.SHA256,
                               RsaTestVectors.SHA384, RsaTestVectors.SHA512};
        for (int i = 0; i < algorithms.length; ++i) {
            Signature verifier = Signature.getInstance(algorithms[i]);
            verifier.initVerify(key);
            verifier.update(RsaTestVectors.MESSAGE);
            check(verifier.verify(signatures[i]), "valid signature verifies");
            byte[] changed = Arrays.copyOf(RsaTestVectors.MESSAGE, RsaTestVectors.MESSAGE.length);
            changed[0] ^= 1;
            verifier.update(changed);
            check(!verifier.verify(signatures[i]), "changed message is rejected");
            changed = Arrays.copyOf(signatures[i], signatures[i].length); changed[changed.length - 1] ^= 1;
            verifier.update(RsaTestVectors.MESSAGE);
            check(!verifier.verify(changed), "changed signature is rejected");
            verifier.update(RsaTestVectors.MESSAGE, 0, 5);
            for (int n = 5; n < RsaTestVectors.MESSAGE.length; ++n) verifier.update(RsaTestVectors.MESSAGE[n]);
            byte[] wrapped = new byte[signatures[i].length + 4];
            System.arraycopy(signatures[i], 0, wrapped, 2, signatures[i].length);
            check(verifier.verify(wrapped, 2, signatures[i].length), "slices and repeated verification reset");
            verifier.update((byte) 1); verifier.initVerify(key); verifier.update(RsaTestVectors.MESSAGE);
            check(verifier.verify(signatures[i]), "reinitialization resets pending input");
        }
        Signature verifier = Signature.getInstance("sha256WITHrsa");
        try { verifier.update((byte) 1); check(false, "uninitialized update accepted"); }
        catch (SignatureException expected) { check(true, "uninitialized update rejected"); }
        try { verifier.verify(RsaTestVectors.SHA256); check(false, "uninitialized verify accepted"); }
        catch (SignatureException expected) { check(true, "uninitialized verification rejected"); }
        verifier.initVerify(key);
        try { verifier.update(RsaTestVectors.MESSAGE, -1, 1); check(false, "negative offset accepted"); }
        catch (IllegalArgumentException expected) { check(true, "negative offset rejected"); }
        try { verifier.update(RsaTestVectors.MESSAGE, 1, Integer.MAX_VALUE); check(false, "overflow range accepted"); }
        catch (IllegalArgumentException expected) { check(true, "overflow range rejected"); }
        verifier.update(RsaTestVectors.MESSAGE);
        check(verifier.verify(RsaTestVectors.SHA256), "invalid update preserves input state");
        try { Signature.getInstance("UnknownSignature"); check(false, "unknown signature accepted"); }
        catch (NoSuchAlgorithmException expected) { check(true, "unknown signature rejected"); }
        try { KeyFactory.getInstance("UnknownKey"); check(false, "unknown key accepted"); }
        catch (NoSuchAlgorithmException expected) { check(true, "unknown key algorithm rejected"); }
        byte[][] invalid = {new byte[0], new byte[]{0x30, 1, 0},
            Arrays.copyOf(RsaTestVectors.KEY, RsaTestVectors.KEY.length - 1),
            Arrays.copyOf(RsaTestVectors.KEY, RsaTestVectors.KEY.length + 1), RsaTestVectors.EC_KEY};
        for (byte[] bytes : invalid) {
            try { factory.generatePublic(new X509EncodedKeySpec(bytes)); check(false, "invalid key accepted"); }
            catch (InvalidKeySpecException expected) { check(true, "malformed or wrong key type rejected"); }
        }
        try { factory.generatePublic(new KeySpec() {}); check(false, "wrong specification accepted"); }
        catch (InvalidKeySpecException expected) { check(true, "unsupported key specification rejected"); }
        try { new X509EncodedKeySpec(null); check(false, "null encoding accepted"); }
        catch (NullPointerException expected) { check(true, "null encoding rejected"); }
        PublicKey bad = new PublicKey() {
            public String getAlgorithm() { return "RSA"; }
            public String getFormat() { return "X.509"; }
            public byte[] getEncoded() { return new byte[0]; }
        };
        try { verifier.initVerify(bad); check(false, "invalid public key accepted"); }
        catch (InvalidKeyException expected) { check(true, "invalid public key rejected"); }
        return checks;
    }
}
