package java.lang;

/**
 * Thay cho java.util.Formatter — chỉ hỗ trợ các directive mà framework/app
 * Android dùng thực tế: %s %d %f %x %X %o %c %b %n %%, có width/precision/flag
 * '-' và '0'.
 */
final class Formatter {

    private Formatter() {
    }

    static String format(String format, Object[] args) {
        if (format == null) {
            throw new NullPointerException("format null");
        }
        StringBuilder out = new StringBuilder(format.length() + 16);
        int argIndex = 0;
        int i = 0;
        int n = format.length();
        while (i < n) {
            char c = format.charAt(i);
            if (c != '%') {
                out.append(c);
                i++;
                continue;
            }
            i++;
            if (i >= n) {
                out.append('%');
                break;
            }
            if (format.charAt(i) == '%') {
                out.append('%');
                i++;
                continue;
            }
            if (format.charAt(i) == 'n') {
                out.append('\n');
                i++;
                continue;
            }

            boolean leftAlign = false;
            boolean zeroPad = false;
            boolean plusSign = false;
            while (i < n) {
                char f = format.charAt(i);
                if (f == '-') {
                    leftAlign = true;
                } else if (f == '0') {
                    zeroPad = true;
                } else if (f == '+') {
                    plusSign = true;
                } else if (f != ' ' && f != ',' && f != '#') {
                    break;
                }
                i++;
            }

            int width = 0;
            while (i < n && Character.isDigit(format.charAt(i))) {
                width = width * 10 + (format.charAt(i) - '0');
                i++;
            }

            int precision = -1;
            if (i < n && format.charAt(i) == '.') {
                i++;
                precision = 0;
                while (i < n && Character.isDigit(format.charAt(i))) {
                    precision = precision * 10 + (format.charAt(i) - '0');
                    i++;
                }
            }

            if (i >= n) {
                break;
            }
            char conv = format.charAt(i);
            i++;

            Object arg = (args != null && argIndex < args.length) ? args[argIndex++] : null;
            String piece = convert(conv, arg, precision, plusSign);
            out.append(pad(piece, width, leftAlign, zeroPad));
        }
        return out.toString();
    }

    private static String convert(char conv, Object arg, int precision, boolean plusSign) {
        switch (conv) {
            case 'd':
                return withSign(longOf(arg), plusSign);
            case 'x':
                return Long.toHexString(longOf(arg));
            case 'X':
                return Long.toHexString(longOf(arg)).toUpperCase();
            case 'o':
                return Long.toOctalString(longOf(arg));
            case 'f':
                return fixed(doubleOf(arg), precision < 0 ? 6 : precision, plusSign);
            case 'e':
            case 'E':
            case 'g':
            case 'G':
                return Double.toString(doubleOf(arg));
            case 'c':
                if (arg instanceof Character) {
                    return String.valueOf(((Character) arg).charValue());
                }
                return String.valueOf((char) intOf(arg));
            case 'b':
                if (arg instanceof Boolean) {
                    return ((Boolean) arg).booleanValue() ? "true" : "false";
                }
                return arg != null ? "true" : "false";
            case 's':
            case 'S': {
                String s = arg == null ? "null" : arg.toString();
                if (precision >= 0 && precision < s.length()) {
                    s = s.substring(0, precision);
                }
                return conv == 'S' ? s.toUpperCase() : s;
            }
            default:
                return arg == null ? "null" : arg.toString();
        }
    }

    private static String withSign(long v, boolean plusSign) {
        String s = Long.toString(v);
        return (plusSign && v >= 0) ? "+" + s : s;
    }

    private static long longOf(Object arg) {
        if (arg instanceof Number) {
            return ((Number) arg).longValue();
        }
        if (arg instanceof Character) {
            return ((Character) arg).charValue();
        }
        return 0;
    }

    private static int intOf(Object arg) {
        return (int) longOf(arg);
    }

    private static double doubleOf(Object arg) {
        if (arg instanceof Number) {
            return ((Number) arg).doubleValue();
        }
        return 0.0;
    }

    private static String fixed(double value, int digits, boolean plusSign) {
        if (Double.isNaN(value)) {
            return "NaN";
        }
        if (Double.isInfinite(value)) {
            return value > 0 ? "Infinity" : "-Infinity";
        }
        boolean negative = value < 0;
        double v = negative ? -value : value;
        double scale = 1.0;
        for (int i = 0; i < digits; i++) {
            scale *= 10.0;
        }
        // Làm tròn nửa lên như %f của C.
        long scaled = (long) (v * scale + 0.5);
        String all = Long.toString(scaled);
        while (all.length() <= digits) {
            all = "0" + all;
        }
        String intPart = digits == 0 ? all : all.substring(0, all.length() - digits);
        String result = intPart;
        if (digits > 0) {
            result = intPart + "." + all.substring(all.length() - digits);
        }
        if (negative) {
            return "-" + result;
        }
        return plusSign ? "+" + result : result;
    }

    private static String pad(String s, int width, boolean leftAlign, boolean zeroPad) {
        int missing = width - s.length();
        if (missing <= 0) {
            return s;
        }
        StringBuilder sb = new StringBuilder(width);
        if (leftAlign) {
            sb.append(s);
            for (int i = 0; i < missing; i++) {
                sb.append(' ');
            }
            return sb.toString();
        }
        // Số âm với '0' padding: dấu trừ phải đứng trước dãy số 0.
        if (zeroPad && s.length() > 0 && s.charAt(0) == '-') {
            sb.append('-');
            for (int i = 0; i < missing; i++) {
                sb.append('0');
            }
            sb.append(s, 1, s.length());
            return sb.toString();
        }
        for (int i = 0; i < missing; i++) {
            sb.append(zeroPad ? '0' : ' ');
        }
        sb.append(s);
        return sb.toString();
    }
}
