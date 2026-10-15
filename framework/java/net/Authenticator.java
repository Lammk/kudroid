package java.net;

public abstract class Authenticator {
    private static Authenticator theAuthenticator;
    private String requestingHost;
    private InetAddress requestingSite;
    private int requestingPort;
    private String requestingProtocol;
    private String requestingPrompt;
    private String requestingScheme;
    private URL requestingURL;
    public enum RequestorType { PROXY, SERVER }
    private RequestorType requestingAuthType;

    public Authenticator() {}

    public static synchronized void setDefault(Authenticator a) {
        theAuthenticator = a;
    }

    public static PasswordAuthentication requestPasswordAuthentication(
            String host, InetAddress addr, int port, String protocol,
            String prompt, String scheme) {
        return null;
    }
    public static PasswordAuthentication requestPasswordAuthentication(
            String host, InetAddress addr, int port, String protocol,
            String prompt, String scheme, URL url, RequestorType reqType) {
        return null;
    }

    protected PasswordAuthentication getPasswordAuthentication() {
        return null;
    }
    protected final String getRequestingHost() { return requestingHost; }
    protected final InetAddress getRequestingSite() { return requestingSite; }
    protected final int getRequestingPort() { return requestingPort; }
    protected final String getRequestingProtocol() { return requestingProtocol; }
    protected final String getRequestingPrompt() { return requestingPrompt; }
    protected final String getRequestingScheme() { return requestingScheme; }
    protected final URL getRequestingURL() { return requestingURL; }
    protected final RequestorType getRequestorType() { return requestingAuthType; }
}
