package libcore.net.ssl;

import java.net.InetAddress;
import java.security.cert.CertificateParsingException;
import java.security.cert.X509Certificate;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;

/** Whether a certificate is valid for a host name (RFC 2818). */
public final class HostnameChecker {
    private HostnameChecker() {}

    public static boolean matches(String host, X509Certificate cert) {
        if (host == null || cert == null) return false;
        String h = host;
        if (h.length() > 2 && h.charAt(0) == '[' && h.charAt(h.length() - 1) == ']') {
            h = h.substring(1, h.length() - 1);
        }
        Collection<List<?>> names;
        try {
            names = cert.getSubjectAlternativeNames();
        } catch (CertificateParsingException e) {
            names = null;
        }
        byte[] ip = literal(h);
        if (ip != null) {
            if (names != null) {
                for (List<?> n : names) {
                    if (((Integer) n.get(0)).intValue() != 7) continue;
                    byte[] san = literal((String) n.get(1));
                    if (san != null && Arrays.equals(ip, san)) return true;
                }
            }
            return false;
        }
        String lower = trimDot(h.toLowerCase());
        boolean sawDns = false;
        if (names != null) {
            for (List<?> n : names) {
                if (((Integer) n.get(0)).intValue() != 2) continue;
                sawDns = true;
                if (matchName(lower, trimDot(((String) n.get(1)).toLowerCase()))) return true;
            }
        }
        if (sawDns) return false;  // a SAN of any DNS name overrides the common name
        String cn = commonName(cert.getSubjectX500Principal().getName());
        return cn != null && matchName(lower, trimDot(cn.toLowerCase()));
    }

    private static String trimDot(String s) {
        return s.endsWith(".") ? s.substring(0, s.length() - 1) : s;
    }

    private static byte[] literal(String s) {
        boolean numeric = s.indexOf(':') >= 0;
        if (!numeric) {
            numeric = s.length() > 0;
            for (int i = 0; i < s.length(); i++) {
                char c = s.charAt(i);
                if (!((c >= '0' && c <= '9') || c == '.')) {
                    numeric = false;
                    break;
                }
            }
        }
        if (!numeric) return null;
        try {
            return InetAddress.getByName(s).getAddress();
        } catch (java.io.IOException e) {
            return null;
        }
    }

    /** The most specific common name of an RFC 2253 string (the first CN, as it is written last-first). */
    private static String commonName(String dn) {
        int i = 0;
        while (i < dn.length()) {
            int comma = i;
            while (comma < dn.length() && dn.charAt(comma) != ',') {
                if (dn.charAt(comma) == '\\') comma++;
                comma++;
            }
            String rdn = dn.substring(i, Math.min(comma, dn.length()));
            if (rdn.regionMatches(0, "CN=", 0, 3) || rdn.regionMatches(0, "cn=", 0, 3)) {
                String v = rdn.substring(3);
                StringBuilder sb = new StringBuilder();
                for (int k = 0; k < v.length(); k++) {
                    char c = v.charAt(k);
                    if (c == '\\' && k + 1 < v.length()) c = v.charAt(++k);
                    sb.append(c);
                }
                return sb.toString();
            }
            i = comma + 1;
        }
        return null;
    }

    static boolean matchName(String host, String pattern) {
        if (host.length() == 0 || pattern.length() == 0) return false;
        if (pattern.indexOf('*') < 0) return host.equals(pattern);
        // One wildcard, alone in the leftmost label, covering exactly one label.
        if (!pattern.startsWith("*.") || pattern.indexOf('*', 1) >= 0) return false;
        String suffix = pattern.substring(1);              // ".example.com"
        if (suffix.indexOf('.', 1) < 0) return false;      // no "*.com"
        if (!host.endsWith(suffix)) return false;
        String label = host.substring(0, host.length() - suffix.length());
        return label.length() > 0 && label.indexOf('.') < 0;
    }
}
