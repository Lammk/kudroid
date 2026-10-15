package javax.net.ssl;

public class SSLParameters {
    private String[] cipherSuites;
    private String[] protocols;
    private String[] applicationProtocols = new String[0];
    private boolean wantClientAuth;
    private boolean needClientAuth;
    private String endpointIdentificationAlgorithm;

    public SSLParameters() {}
    public SSLParameters(String[] cipherSuites) { setCipherSuites(cipherSuites); }
    public SSLParameters(String[] cipherSuites, String[] protocols) {
        setCipherSuites(cipherSuites);
        setProtocols(protocols);
    }

    public String[] getCipherSuites() { return cipherSuites == null ? null : cipherSuites.clone(); }
    public void setCipherSuites(String[] cipherSuites) {
        this.cipherSuites = cipherSuites == null ? null : cipherSuites.clone();
    }
    public String[] getProtocols() { return protocols == null ? null : protocols.clone(); }
    public void setProtocols(String[] protocols) {
        this.protocols = protocols == null ? null : protocols.clone();
    }
    public boolean getWantClientAuth() { return wantClientAuth; }
    public void setWantClientAuth(boolean wantClientAuth) {
        this.wantClientAuth = wantClientAuth;
        this.needClientAuth = false;
    }
    public boolean getNeedClientAuth() { return needClientAuth; }
    public void setNeedClientAuth(boolean needClientAuth) {
        this.needClientAuth = needClientAuth;
        this.wantClientAuth = false;
    }
    public String getEndpointIdentificationAlgorithm() { return endpointIdentificationAlgorithm; }
    public void setEndpointIdentificationAlgorithm(String algorithm) { this.endpointIdentificationAlgorithm = algorithm; }
    public String[] getApplicationProtocols() { return applicationProtocols.clone(); }
    public void setApplicationProtocols(String[] protocols) {
        this.applicationProtocols = protocols == null ? new String[0] : protocols.clone();
    }
}
