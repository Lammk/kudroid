package android.webkit;

import android.net.Uri;
import java.util.Map;

public interface WebResourceRequest {
    Uri getUrl();
    String getMethod();
    Map<String, String> getRequestHeaders();
    boolean isForMainFrame();
    boolean isRedirect();
    boolean hasGesture();
}
