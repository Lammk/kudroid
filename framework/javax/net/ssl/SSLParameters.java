package javax.net.ssl;

public class SSLParameters {
    private String[] cipherSuites;
    private String[] protocols;
    private boolean wantClientAuth;
    private boolean needClientAuth;
    private String endpointIdentificationAlgorithm;

    public SSLParameters() {}
    public SSLParameters(String[] cipherSuites) { setCipherSuites(cipherSuites); }
    public SSLParameters(String[] cipherSuites, String[] protocols) {
        setCipherSuites(cipherSuites);
        setProtocols(protocols);
    }
    public String[] getCipherSuites() { return cipherSuites; }
    public void setCipherSuites(String[] cipherSuites) { this.cipherSuites = cipherSuites; }
    public String[] getProtocols() { return protocols; }
    public void setProtocols(String[] protocols) { this.protocols = protocols; }
    public boolean getWantClientAuth() { return wantClientAuth; }
    public void setWantClientAuth(boolean wantClientAuth) { this.wantClientAuth = wantClientAuth; }
    public boolean getNeedClientAuth() { return needClientAuth; }
    public void setNeedClientAuth(boolean needClientAuth) { this.needClientAuth = needClientAuth; }
    public String getEndpointIdentificationAlgorithm() { return endpointIdentificationAlgorithm; }
    public void setEndpointIdentificationAlgorithm(String algorithm) { this.endpointIdentificationAlgorithm = algorithm; }
}
