package java.lang;

public final class Math {

    public static final double E = 2.718281828459045;
    public static final double PI = 3.141592653589793;

    private Math() {
    }

    public static int abs(int a) {
        return a < 0 ? -a : a;
    }

    public static long abs(long a) {
        return a < 0 ? -a : a;
    }

    public static float abs(float a) {
        return a < 0 ? -a : a;
    }

    public static double abs(double a) {
        return a < 0 ? -a : a;
    }

    public static int max(int a, int b) {
        return a > b ? a : b;
    }

    public static long max(long a, long b) {
        return a > b ? a : b;
    }

    public static float max(float a, float b) {
        return a > b ? a : b;
    }

    public static double max(double a, double b) {
        return a > b ? a : b;
    }

    public static int min(int a, int b) {
        return a < b ? a : b;
    }

    public static long min(long a, long b) {
        return a < b ? a : b;
    }

    public static float min(float a, float b) {
        return a < b ? a : b;
    }

    public static double min(double a, double b) {
        return a < b ? a : b;
    }

    public static int signum(int a) {
        return a > 0 ? 1 : (a < 0 ? -1 : 0);
    }

    public static double signum(double a) {
        return a > 0 ? 1.0 : (a < 0 ? -1.0 : a);
    }

    public static int round(float a) {
        return (int) floor(a + 0.5f);
    }

    public static long round(double a) {
        return (long) floor(a + 0.5d);
    }

    public static int floorDiv(int a, int b) {
        int q = a / b;
        if ((a ^ b) < 0 && q * b != a) {
            q--;
        }
        return q;
    }

    public static int floorMod(int a, int b) {
        return a - floorDiv(a, b) * b;
    }

    public static long floorDiv(long a, long b) {
        long q = a / b;
        if ((a ^ b) < 0 && q * b != a) {
            q--;
        }
        return q;
    }

    public static long floorMod(long a, long b) {
        return a - floorDiv(a, b) * b;
    }

    public static int addExact(int a, int b) {
        int r = a + b;
        if (((a ^ r) & (b ^ r)) < 0) {
            throw new ArithmeticException("integer overflow");
        }
        return r;
    }

    public static int multiplyExact(int a, int b) {
        long r = (long) a * (long) b;
        if ((int) r != r) {
            throw new ArithmeticException("integer overflow");
        }
        return (int) r;
    }

    public static int toIntExact(long value) {
        if ((int) value != value) {
            throw new ArithmeticException("integer overflow");
        }
        return (int) value;
    }

    public static double toRadians(double angdeg) {
        return angdeg / 180.0 * PI;
    }

    public static double toDegrees(double angrad) {
        return angrad * 180.0 / PI;
    }

    public static double random() {
        return randomImpl();
    }

    public static native double sin(double a);

    public static native double cos(double a);

    public static native double tan(double a);

    public static native double asin(double a);

    public static native double acos(double a);

    public static native double atan(double a);

    public static native double atan2(double y, double x);

    public static native double sinh(double a);

    public static native double cosh(double a);

    public static native double tanh(double a);

    public static native double exp(double a);

    public static native double log(double a);

    public static native double log10(double a);

    public static native double sqrt(double a);

    public static native double cbrt(double a);

    public static native double pow(double a, double b);

    public static native double ceil(double a);

    public static native double floor(double a);

    public static native double rint(double a);

    public static native double hypot(double x, double y);

    public static native double IEEEremainder(double f1, double f2);

    private static native double randomImpl();
}
