package java.net;

import java.util.List;

public interface CookieStore {
    void add(URI uri, HttpCookie cookie);
    List<HttpCookie> get(URI uri);
    List<HttpCookie> getCookies();
    List<URI> getURIs();
    boolean remove(URI uri, HttpCookie cookie);
    boolean removeAll();
}
