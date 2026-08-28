package java.net;

import java.io.Serializable;

public final class URI implements Comparable<URI>, Serializable {
    private static final long serialVersionUID = -6052424284110960213L;
    private final String string;

    public URI(String str) throws URISyntaxException {
        this.string = str;
    }
    public static URI create(String str) {
        try {
            return new URI(str);
        } catch (URISyntaxException x) {
            throw new IllegalArgumentException(x.getMessage(), x);
        }
    }
    public String getScheme() { return "http"; }
    public String getHost() { return "localhost"; }
    public int getPort() { return -1; }
    public String getPath() { return string; }
    public String getQuery() { return null; }
    public String toString() { return string; }
    public int compareTo(URI that) { return this.string.compareTo(that.string); }
    public URL toURL() throws MalformedURLException { return new URL(string); }
}
