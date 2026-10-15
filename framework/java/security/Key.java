package java.security;

import java.io.Serializable;

public interface Key extends Serializable {
    String getAlgorithm();
    String getFormat();
    byte[] getEncoded();
}
