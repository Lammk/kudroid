package javax.crypto.spec;

import java.security.spec.KeySpec;
import java.util.Arrays;

public class PBEKeySpec implements KeySpec {
    private char[] password;
    private final byte[] salt;
    private final int iterationCount;
    private final int keyLength;

    public PBEKeySpec(char[] password) {
        this.password = password == null ? new char[0] : Arrays.copyOf(password, password.length);
        salt = null;
        iterationCount = keyLength = 0;
    }

    public PBEKeySpec(char[] password, byte[] salt, int iterationCount) {
        this(password, salt, iterationCount, 0, false);
    }

    public PBEKeySpec(char[] password, byte[] salt, int iterationCount, int keyLength) {
        this(password, salt, iterationCount, keyLength, true);
    }

    private PBEKeySpec(char[] password, byte[] salt, int iterationCount, int keyLength, boolean hasLength) {
        if (salt == null) throw new NullPointerException("salt");
        if (salt.length == 0 || iterationCount <= 0 || (hasLength && keyLength <= 0))
            throw new IllegalArgumentException("Invalid PBE parameters");
        this.password = password == null ? new char[0] : Arrays.copyOf(password, password.length);
        this.salt = Arrays.copyOf(salt, salt.length);
        this.iterationCount = iterationCount;
        this.keyLength = keyLength;
    }

    public final void clearPassword() {
        if (password != null) {
            for (int i = 0; i < password.length; i++) password[i] = 0;
            password = null;
        }
    }

    public final char[] getPassword() {
        if (password == null) throw new IllegalStateException("Password has been cleared");
        return Arrays.copyOf(password, password.length);
    }
    public final byte[] getSalt() { return salt == null ? null : Arrays.copyOf(salt, salt.length); }
    public final int getIterationCount() { return iterationCount; }
    public final int getKeyLength() { return keyLength; }
}
