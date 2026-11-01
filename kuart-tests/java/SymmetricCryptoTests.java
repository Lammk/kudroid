import java.security.*;
import java.security.spec.*;
import java.util.Arrays;
import javax.crypto.*;
import javax.crypto.spec.*;

public class SymmetricCryptoTests {
    private static int checks;
    private static void check(boolean value, String message) {
        checks++;
        if (!value) throw new AssertionError(message);
    }
    private static byte[] hex(String text) {
        byte[] bytes = new byte[text.length() / 2];
        for (int i = 0; i < bytes.length; i++) bytes[i] = (byte)Integer.parseInt(text.substring(i * 2, i * 2 + 2), 16);
        return bytes;
    }
    public static int run() throws Exception {
        checks = 0;
        String name = "PBEWITHSHAAND256BITAES-CBC-BC";
        SecretKeyFactory factory = SecretKeyFactory.getInstance(name);
        check(name.equals(factory.getAlgorithm()), "factory algorithm preserved");
        String[] passwords = {"password", "", "\u03A3\u00E9\u4E2D\uD83D\uDE00", "ab\u0000cd",
            "a password longer than thirty-two UTF-16 code units for repeated blocks"};
        int[] iterations = {1, 2, 1024, 3, 7};
        String[] keys = {
            "4f11857440a9b78e842805246db0d14c2e7ff7cd1d96a83ddea21ec13d9ee093",
            "a7d73943e485888b80d15b288a38dd1c64fa97527ab393ebec9f6dd90e2c00c7",
            "01a516bbba968ac044d5bba9f3383a0cd8f15520699df6fb6b2e8abffe129320",
            "3aa7b2cdfd08b37be19fc00b231fb61aa8e0de6643e50ae21dd1668abc6942b8",
            "442cb4c0348db355258a73d104d0882d33a321e3ad77dbbc2f81543c113cfbee"};
        String[] ciphertexts = {
            "9b0ca88ef580aa9281f280ab9573f45e7a080e65cbb330a7230f927cdbe8899d",
            "28a1d7f6bda721fd7d862f4df072c35829ff503a73de1f6eb71a72b8307c597f",
            "b090d91200f1972a09c6b420677375fbec1adcae9e1f54c651fe270db244da9c",
            "139a8b4d9c96002cedd3c08da925b2b1853e3694afabbdca789a3973b25939ad",
            "9c3b329fd1aa18e357719235a3814f1ede767b667cfe7f7d16e90a6009c8800d"};
        byte[] iv = new byte[16];
        for (int i = 0; i < iv.length; i++) iv[i] = (byte)i;
        byte[] message = "generic test payload \u00E9".getBytes("UTF-8");
        for (int i = 0; i < passwords.length; i++) {
            byte[] salt = new byte[i == 4 ? 71 : 8];
            for (int j = 0; j < salt.length; j++) salt[j] = (byte)(j * 17 + 3);
            SecretKey secret = factory.generateSecret(new PBEKeySpec(passwords[i].toCharArray(), salt, iterations[i], i == 3 ? 128 : 256));
            check(Arrays.equals(secret.getEncoded(), hex(keys[i])), "PKCS12 SHA-1 independent provider vector");
            byte[] copy = secret.getEncoded(); copy[0] ^= 1;
            check(Arrays.equals(secret.getEncoded(), hex(keys[i])), "derived key defensive encoding");
            SecretKeySpec aes = new SecretKeySpec(secret.getEncoded(), "AES");
            Cipher cipher = Cipher.getInstance("AES/CBC/PKCS5Padding");
            cipher.init(Cipher.ENCRYPT_MODE, aes, new IvParameterSpec(iv));
            check(cipher.getBlockSize() == 16 && cipher.getOutputSize(message.length) == 32, "AES block and output sizes");
            check(Arrays.equals(cipher.doFinal(message), hex(ciphertexts[i])), "AES CBC independent provider vector");
            check(Arrays.equals(cipher.doFinal(message), hex(ciphertexts[i])), "successful final resets operation to initialized state");
            cipher.init(Cipher.DECRYPT_MODE, aes, new IvParameterSpec(iv));
            check(Arrays.equals(cipher.doFinal(hex(ciphertexts[i])), message), "AES CBC decrypt vector");
            byte[] wrapped = new byte[message.length + 4];
            System.arraycopy(message, 0, wrapped, 2, message.length);
            cipher.init(Cipher.ENCRYPT_MODE, aes, new IvParameterSpec(iv));
            check(Arrays.equals(cipher.doFinal(wrapped, 2, message.length), hex(ciphertexts[i])), "final input slice respected");
        }
        for (int keyLength : new int[]{16, 24, 32}) {
            SecretKeySpec aes = new SecretKeySpec(new byte[keyLength], "AES");
            Cipher cipher = Cipher.getInstance("AES/CBC/PKCS5Padding");
            for (int length : new int[]{0, 1, 15, 16, 17, 31, 32}) {
                cipher.init(Cipher.ENCRYPT_MODE, aes, new IvParameterSpec(iv));
                byte[] input = new byte[length];
                for (int n = 0; n < length; n++) input[n] = (byte)n;
                byte[] encrypted = cipher.doFinal(input);
                check(encrypted.length == (length / 16 + 1) * 16, "PKCS padding includes aligned input");
                cipher.init(Cipher.DECRYPT_MODE, aes, new IvParameterSpec(iv));
                check(Arrays.equals(cipher.doFinal(encrypted), input), "all AES key and block lengths round trip");
            }
        }
        char[] password = "password".toCharArray(); byte[] salt = {1, 2, 3};
        PBEKeySpec spec = new PBEKeySpec(password, salt, 2, 256);
        password[0] = 0; salt[0] = 0;
        check(spec.getPassword()[0] == 'p' && spec.getSalt()[0] == 1, "PBE constructor copies inputs");
        spec.getPassword()[0] = 0; spec.getSalt()[0] = 0;
        check(spec.getPassword()[0] == 'p' && spec.getSalt()[0] == 1, "PBE getters copy inputs");
        spec.clearPassword(); spec.clearPassword();
        try { spec.getPassword(); check(false, "cleared password read accepted"); }
        catch (IllegalStateException expected) { check(true, "cleared password cannot be read"); }
        check(new PBEKeySpec(null).getPassword().length == 0, "null password is empty");
        try { new PBEKeySpec(password, null, 1, 256); check(false, "null salt accepted"); }
        catch (NullPointerException expected) { check(true, "null salt rejected"); }
        try { new PBEKeySpec(password, new byte[0], 1, 256); check(false, "empty salt accepted"); }
        catch (IllegalArgumentException expected) { check(true, "empty salt rejected"); }
        try { new PBEKeySpec(password, salt, 0, 256); check(false, "zero iterations accepted"); }
        catch (IllegalArgumentException expected) { check(true, "zero iterations rejected"); }
        try { new PBEKeySpec(password, salt, 1, 0); check(false, "zero key length accepted"); }
        catch (IllegalArgumentException expected) { check(true, "zero key length rejected"); }
        try { factory.generateSecret(new X509EncodedKeySpec(new byte[1])); check(false, "wrong key spec accepted"); }
        catch (InvalidKeySpecException expected) { check(true, "wrong key spec rejected"); }
        try { factory.generateSecret(new PBEKeySpec(password)); check(false, "unsalted derivation accepted"); }
        catch (InvalidKeySpecException expected) { check(true, "unsalted derivation rejected"); }
        try { SecretKeyFactory.getInstance("UnsupportedPBE"); check(false, "unknown KDF accepted"); }
        catch (NoSuchAlgorithmException expected) { check(true, "unknown KDF rejected"); }

        byte[] raw = new byte[16]; raw[0] = 42;
        SecretKeySpec aes = new SecretKeySpec(raw, "AES"); raw[0] = 0;
        check(aes.getEncoded()[0] == 42 && "RAW".equals(aes.getFormat()), "AES specification defensive encoding");
        check(aes.equals(new SecretKeySpec(aes.getEncoded(), "aes")) &&
              aes.hashCode() == new SecretKeySpec(aes.getEncoded(), "aes").hashCode(), "secret key equality and hash");
        IvParameterSpec params = new IvParameterSpec(iv); iv[0] = 42;
        check(params.getIV()[0] == 0, "IV constructor copies input");
        params.getIV()[0] = 42; check(params.getIV()[0] == 0, "IV getter copies input");
        Cipher cipher = Cipher.getInstance("AES/CBC/PKCS5Padding");
        try { cipher.doFinal(message); check(false, "uninitialized final accepted"); }
        catch (IllegalStateException expected) { check(true, "uninitialized final rejected"); }
        try { cipher.init(1, new SecretKeySpec(new byte[15], "AES"), params); check(false, "wrong AES key length accepted"); }
        catch (InvalidKeyException expected) { check(true, "wrong AES key length rejected"); }
        try { cipher.init(1, aes, new IvParameterSpec(new byte[15])); check(false, "wrong IV length accepted"); }
        catch (InvalidAlgorithmParameterException expected) { check(true, "wrong IV length rejected"); }
        try { cipher.init(2, aes); check(false, "decrypt without IV accepted"); }
        catch (InvalidKeyException expected) { check(true, "decrypt without IV rejected"); }
        cipher.init(2, aes, params);
        try { cipher.doFinal(new byte[15]); check(false, "unaligned ciphertext accepted"); }
        catch (IllegalBlockSizeException expected) { check(true, "unaligned ciphertext rejected"); }
        cipher.init(2, aes, params);
        try { cipher.doFinal(new byte[16]); check(false, "invalid padding accepted"); }
        catch (BadPaddingException expected) { check(true, "invalid padding rejected"); }
        cipher.init(1, aes, params);
        try { cipher.doFinal(message, 1, Integer.MAX_VALUE); check(false, "overflow slice accepted"); }
        catch (IllegalArgumentException expected) { check(true, "overflow slice rejected"); }
        byte[] empty = cipher.doFinal();
        check(empty.length == 16, "empty encryption produces padding block");
        cipher.init(2, aes, params);
        check(cipher.doFinal(empty).length == 0, "empty plaintext decrypts");
        check(cipher.doFinal().length == 0, "empty decrypt follows Conscrypt behavior");
        check(cipher.getOutputSize(17) == 32, "decrypt output allocation rounds to block size");
        cipher.init(1, aes);
        check(cipher.getIV().length == 16, "encryption without IV uses native random source");
        byte[] savedIV = cipher.getIV(); savedIV[0] ^= 1;
        check(!Arrays.equals(savedIV, cipher.getIV()), "cipher IV getter copies");
        try { Cipher.getInstance("Unknown/CBC/PKCS5Padding"); check(false, "unknown cipher accepted"); }
        catch (NoSuchAlgorithmException expected) { check(true, "unknown cipher rejected"); }
        try { Cipher.getInstance("AES/CBC/UnknownPadding"); check(false, "unknown padding accepted"); }
        catch (NoSuchPaddingException expected) { check(true, "unknown padding rejected"); }
        try { Cipher.getInstance("AES/CBC/PKCS5Padding/"); check(false, "trailing transformation component accepted"); }
        catch (NoSuchAlgorithmException expected) { check(true, "invalid transformation rejected"); }
        try { new SecretKeySpec(new byte[16], -1, 1, "AES"); check(false, "negative key offset accepted"); }
        catch (ArrayIndexOutOfBoundsException expected) { check(true, "negative key offset rejected"); }
        return checks;
    }
}
