package javax.crypto.spec;

import java.security.spec.AlgorithmParameterSpec;
import java.util.Arrays;

public class IvParameterSpec implements AlgorithmParameterSpec {
    private final byte[] iv;
    public IvParameterSpec(byte[] iv) { this(iv, 0, iv == null ? 0 : iv.length); }
    public IvParameterSpec(byte[] iv, int offset, int length) {
        if (iv == null) throw new IllegalArgumentException("Missing IV");
        if (offset < 0 || length < 0 || offset > iv.length - length)
            throw new ArrayIndexOutOfBoundsException();
        this.iv = new byte[length];
        System.arraycopy(iv, offset, this.iv, 0, length);
    }
    public byte[] getIV() { return Arrays.copyOf(iv, iv.length); }
}
