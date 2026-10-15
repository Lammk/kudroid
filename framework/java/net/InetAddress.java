package java.net;

import java.io.IOException;
import java.io.Serializable;
import java.util.HashMap;
import java.util.Map;
import libcore.io.KuNet;

/** An IPv4 or IPv6 address, resolved through the host's resolver. */
public class InetAddress implements Serializable {
    private static final long serialVersionUID = 3286316764910316507L;

    // Resolved names are reused briefly; a network change makes older answers wrong.
    private static final long CACHE_MILLIS = 10000;
    private static final Map<String, Object[]> sCache = new HashMap<String, Object[]>();

    String hostName;
    byte[] addr;

    InetAddress() {}

    InetAddress(String hostName, byte[] addr) {
        this.hostName = hostName;
        this.addr = addr;
    }

    // ── construction ─────────────────────────────────────────────────────────

    /** An Inet4Address for 4 bytes; for 16 an Inet6Address, or an Inet4Address if v4-mapped. */
    static InetAddress make(String host, byte[] a) throws UnknownHostException {
        if (a == null) throw new UnknownHostException("addr is null");
        if (a.length == 4) return new Inet4Address(host, a.clone());
        if (a.length == 16) {
            boolean mapped = (a[10] == (byte) 0xff && a[11] == (byte) 0xff);
            for (int i = 0; mapped && i < 10; i++) {
                if (a[i] != 0) mapped = false;
            }
            if (mapped) {
                byte[] v4 = new byte[4];
                System.arraycopy(a, 12, v4, 0, 4);
                return new Inet4Address(host, v4);
            }
            return new Inet6Address(host, a.clone());
        }
        throw new UnknownHostException("addr is of illegal length");
    }

    public static InetAddress getByAddress(byte[] addr) throws UnknownHostException {
        return make(null, addr);
    }

    public static InetAddress getByAddress(String host, byte[] addr) throws UnknownHostException {
        return make(host, addr);
    }

    public static InetAddress getLoopbackAddress() {
        return new Inet4Address("localhost", new byte[] {127, 0, 0, 1});
    }

    static InetAddress anyLocalAddress() {
        return new Inet4Address("0.0.0.0", new byte[4]);
    }

    public static InetAddress getLocalHost() throws UnknownHostException {
        byte[] name = new byte[256];
        int n = KuNet.hostName(name);
        if (n > 0) {
            try {
                return getByName(new String(name, 0, n));
            } catch (UnknownHostException ignored) {
                // fall through to loopback
            }
        }
        return getLoopbackAddress();
    }

    public static InetAddress getByName(String host) throws UnknownHostException {
        if (host == null || host.length() == 0) return getLoopbackAddress();
        String h = host;
        if (h.length() > 2 && h.charAt(0) == '[' && h.charAt(h.length() - 1) == ']') {
            h = h.substring(1, h.length() - 1);
        }
        byte[] literal = parseLiteral(h);
        if (literal != null) return make(null, literal);
        return getAllByName(host)[0];
    }

    public static InetAddress[] getAllByName(String host) throws UnknownHostException {
        if (host == null || host.length() == 0) return new InetAddress[] {getLoopbackAddress()};
        String h = host;
        if (h.length() > 2 && h.charAt(0) == '[' && h.charAt(h.length() - 1) == ']') {
            h = h.substring(1, h.length() - 1);
        }
        byte[] literal = parseLiteral(h);
        if (literal != null) return new InetAddress[] {make(null, literal)};

        long now = System.currentTimeMillis();
        synchronized (sCache) {
            Object[] hit = sCache.get(host);
            if (hit != null && now - ((Long) hit[0]).longValue() < CACHE_MILLIS) {
                return ((InetAddress[]) hit[1]).clone();
            }
        }
        byte[] out = new byte[17 * 32];
        int count = KuNet.resolve(host, out);
        if (count <= 0) {
            throw new UnknownHostException("Unable to resolve host \"" + host
                    + "\": No address associated with hostname");
        }
        // IPv4 first, as the platform default prefers.
        InetAddress[] v4 = new InetAddress[count];
        InetAddress[] v6 = new InetAddress[count];
        int n4 = 0;
        int n6 = 0;
        for (int i = 0; i < count; i++) {
            int len = out[i * 17] & 0xff;
            byte[] a = new byte[len];
            System.arraycopy(out, i * 17 + 1, a, 0, len);
            InetAddress ia = make(host, a);
            if (ia instanceof Inet4Address) v4[n4++] = ia; else v6[n6++] = ia;
        }
        InetAddress[] all = new InetAddress[n4 + n6];
        System.arraycopy(v4, 0, all, 0, n4);
        System.arraycopy(v6, 0, all, n4, n6);
        synchronized (sCache) {
            sCache.put(host, new Object[] {Long.valueOf(now), all});
        }
        return all.clone();
    }

    // ── textual forms ────────────────────────────────────────────────────────

    /** The 4 or 16 address bytes of a numeric host, or null when host is a name. */
    static byte[] parseLiteral(String s) {
        if (s.indexOf(':') >= 0) return parseV6(s);
        return parseV4(s);
    }

    private static byte[] parseV4(String s) {
        byte[] out = new byte[4];
        int part = 0;
        int value = -1;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c >= '0' && c <= '9') {
                value = (value < 0 ? 0 : value * 10) + (c - '0');
                if (value > 255) return null;
            } else if (c == '.') {
                if (value < 0 || part >= 3) return null;
                out[part++] = (byte) value;
                value = -1;
            } else {
                return null;
            }
        }
        if (value < 0 || part != 3) return null;
        out[3] = (byte) value;
        return out;
    }

    private static byte[] parseV6(String s) {
        int pct = s.indexOf('%');
        if (pct >= 0) s = s.substring(0, pct);
        int dbl = s.indexOf("::");
        if (dbl >= 0 && s.indexOf("::", dbl + 1) >= 0) return null;
        String head = dbl >= 0 ? s.substring(0, dbl) : s;
        String tail = dbl >= 0 ? s.substring(dbl + 2) : "";
        int[] headGroups = new int[8];
        int[] tailGroups = new int[8];
        int nh = groups(head, headGroups, dbl < 0);
        if (nh < 0) return null;
        int nt = dbl >= 0 ? groups(tail, tailGroups, true) : 0;
        if (nt < 0) return null;
        if (dbl < 0 && nh != 8) return null;
        if (dbl >= 0 && nh + nt > 7) return null;
        byte[] out = new byte[16];
        for (int i = 0; i < nh; i++) {
            out[i * 2] = (byte) (headGroups[i] >> 8);
            out[i * 2 + 1] = (byte) headGroups[i];
        }
        for (int i = 0; i < nt; i++) {
            int g = 8 - nt + i;
            out[g * 2] = (byte) (tailGroups[i] >> 8);
            out[g * 2 + 1] = (byte) tailGroups[i];
        }
        return out;
    }

    /** Parses colon-separated hex groups (a trailing dotted quad counts as two); -1 on error. */
    private static int groups(String s, int[] out, boolean allowEmpty) {
        if (s.length() == 0) return allowEmpty ? 0 : -1;
        int n = 0;
        int start = 0;
        while (start <= s.length()) {
            int end = s.indexOf(':', start);
            boolean last = end < 0;
            if (last) end = s.length();
            String g = s.substring(start, end);
            if (g.length() == 0) return -1;
            if (g.indexOf('.') >= 0) {
                if (!last || n > 6) return -1;
                byte[] v4 = parseV4(g);
                if (v4 == null) return -1;
                out[n++] = ((v4[0] & 0xff) << 8) | (v4[1] & 0xff);
                out[n++] = ((v4[2] & 0xff) << 8) | (v4[3] & 0xff);
                return n;
            }
            if (g.length() > 4 || n >= 8) return -1;
            int v = 0;
            for (int i = 0; i < g.length(); i++) {
                int d = Character.digit(g.charAt(i), 16);
                if (d < 0) return -1;
                v = v * 16 + d;
            }
            out[n++] = v;
            if (last) break;
            start = end + 1;
        }
        return n;
    }

    public String getHostAddress() {
        if (addr.length == 4) {
            return (addr[0] & 0xff) + "." + (addr[1] & 0xff) + "." + (addr[2] & 0xff) + "."
                    + (addr[3] & 0xff);
        }
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 8; i++) {
            if (i > 0) sb.append(':');
            sb.append(Integer.toHexString(((addr[i * 2] & 0xff) << 8) | (addr[i * 2 + 1] & 0xff)));
        }
        return sb.toString();
    }

    public byte[] getAddress() { return addr.clone(); }

    // ── names ────────────────────────────────────────────────────────────────

    public String getHostName() {
        if (hostName != null) return hostName;
        byte[] out = new byte[256];
        int n = KuNet.reverse(addr, out);
        hostName = n > 0 ? new String(out, 0, n) : getHostAddress();
        return hostName;
    }

    public String getCanonicalHostName() {
        byte[] out = new byte[256];
        int n = KuNet.reverse(addr, out);
        return n > 0 ? new String(out, 0, n) : getHostAddress();
    }

    // ── classification ───────────────────────────────────────────────────────

    public boolean isLoopbackAddress() {
        if (addr.length == 4) return (addr[0] & 0xff) == 127;
        for (int i = 0; i < 15; i++) {
            if (addr[i] != 0) return false;
        }
        return addr[15] == 1;
    }

    public boolean isAnyLocalAddress() {
        for (int i = 0; i < addr.length; i++) {
            if (addr[i] != 0) return false;
        }
        return true;
    }

    public boolean isMulticastAddress() {
        if (addr.length == 4) return (addr[0] & 0xf0) == 0xe0;
        return (addr[0] & 0xff) == 0xff;
    }

    public boolean isLinkLocalAddress() {
        if (addr.length == 4) return (addr[0] & 0xff) == 169 && (addr[1] & 0xff) == 254;
        return (addr[0] & 0xff) == 0xfe && (addr[1] & 0xc0) == 0x80;
    }

    public boolean isSiteLocalAddress() {
        if (addr.length == 4) {
            int b0 = addr[0] & 0xff;
            int b1 = addr[1] & 0xff;
            return b0 == 10 || (b0 == 172 && (b1 & 0xf0) == 16) || (b0 == 192 && b1 == 168);
        }
        return (addr[0] & 0xff) == 0xfe && (addr[1] & 0xc0) == 0xc0;
    }

    public boolean isMCGlobal() {
        if (!isMulticastAddress()) return false;
        if (addr.length == 4) {
            int b0 = addr[0] & 0xff;
            if (b0 == 224 && addr[1] == 0 && addr[2] == 0) return false;
            return b0 <= 238;
        }
        return (addr[1] & 0x0f) == 0x0e;
    }

    public boolean isReachable(int timeout) throws IOException {
        if (timeout < 0) throw new IllegalArgumentException("timeout < 0");
        if (isLoopbackAddress()) return true;
        int family = addr.length == 16 ? KuNet.AF_INET6 : KuNet.AF_INET;
        int fd = KuNet.socket(family, KuNet.SOCK_STREAM);
        if (fd < 0) return false;
        try {
            // A refusal still proves the host answered.
            int rc = KuNet.connect(fd, addr, 7, timeout > 0 ? timeout : 5000);
            return rc == 0 || rc == -KuNet.ECONNREFUSED;
        } finally {
            KuNet.close(fd);
        }
    }

    public boolean isReachable(NetworkInterface netif, int ttl, int timeout) throws IOException {
        return isReachable(timeout);
    }

    // ── identity ─────────────────────────────────────────────────────────────

    @Override
    public boolean equals(Object obj) {
        if (!(obj instanceof InetAddress)) return false;
        InetAddress o = (InetAddress) obj;
        return java.util.Arrays.equals(addr, o.addr);
    }

    @Override
    public int hashCode() {
        int h = 0;
        for (int i = 0; i < addr.length; i++) {
            h = h * 31 + addr[i];
        }
        return h;
    }

    @Override
    public String toString() {
        return (hostName != null ? hostName : "") + "/" + getHostAddress();
    }
}
