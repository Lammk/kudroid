package javax.security.auth.x500;

import java.io.Serializable;
import java.security.Principal;

/** A distinguished name, held in the RFC 2253 text form the certificate parser produces. */
public final class X500Principal implements Principal, Serializable {
    private static final long serialVersionUID = -500463348111345721L;

    public static final String RFC1779 = "RFC1779";
    public static final String RFC2253 = "RFC2253";
    public static final String CANONICAL = "CANONICAL";

    private final String name;
    private final byte[] encoded;

    /** For the certificate parser: a name already in RFC 2253 form, with its DER. */
    public X500Principal(String rfc2253, byte[] der) {
        this.name = rfc2253;
        this.encoded = der;
    }

    public X500Principal(String name) {
        if (name == null) throw new NullPointerException("name == null");
        this.name = name;
        this.encoded = new byte[0];
    }

    @Override
    public String getName() { return name; }

    public String getName(String format) {
        if (CANONICAL.equals(format)) return name.toLowerCase();
        return name;
    }

    public byte[] getEncoded() { return encoded.clone(); }

    @Override
    public String toString() { return name; }

    @Override
    public boolean equals(Object o) {
        return o instanceof X500Principal && ((X500Principal) o).name.equalsIgnoreCase(name);
    }

    @Override
    public int hashCode() { return name.toLowerCase().hashCode(); }
}
