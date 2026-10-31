package java.util;

public final class Arrays {

    private Arrays() {
    }

    public static <T> List<T> asList(T... items) {
        ArrayList<T> out = new ArrayList<T>(items.length + 1);
        for (int i = 0; i < items.length; i++) {
            out.add(items[i]);
        }
        return out;
    }

    public static void fill(int[] a, int value) {
        for (int i = 0; i < a.length; i++) {
            a[i] = value;
        }
    }

    public static void fill(long[] a, long value) {
        for (int i = 0; i < a.length; i++) {
            a[i] = value;
        }
    }

    public static void fill(byte[] a, byte value) {
        for (int i = 0; i < a.length; i++) {
            a[i] = value;
        }
    }

    public static void fill(char[] a, char value) {
        for (int i = 0; i < a.length; i++) {
            a[i] = value;
        }
    }

    public static void fill(float[] a, float value) {
        for (int i = 0; i < a.length; i++) {
            a[i] = value;
        }
    }

    public static void fill(double[] a, double value) {
        for (int i = 0; i < a.length; i++) {
            a[i] = value;
        }
    }

    public static void fill(boolean[] a, boolean value) {
        for (int i = 0; i < a.length; i++) {
            a[i] = value;
        }
    }

    public static void fill(Object[] a, Object value) {
        for (int i = 0; i < a.length; i++) {
            a[i] = value;
        }
    }

    public static int[] copyOf(int[] a, int newLength) {
        int[] out = new int[newLength];
        System.arraycopy(a, 0, out, 0, Math.min(a.length, newLength));
        return out;
    }

    public static long[] copyOf(long[] a, int newLength) {
        long[] out = new long[newLength];
        System.arraycopy(a, 0, out, 0, Math.min(a.length, newLength));
        return out;
    }

    public static byte[] copyOf(byte[] a, int newLength) {
        byte[] out = new byte[newLength];
        System.arraycopy(a, 0, out, 0, Math.min(a.length, newLength));
        return out;
    }

    public static char[] copyOf(char[] a, int newLength) {
        char[] out = new char[newLength];
        System.arraycopy(a, 0, out, 0, Math.min(a.length, newLength));
        return out;
    }

    public static float[] copyOf(float[] a, int newLength) {
        float[] out = new float[newLength];
        System.arraycopy(a, 0, out, 0, Math.min(a.length, newLength));
        return out;
    }

    public static double[] copyOf(double[] a, int newLength) {
        double[] out = new double[newLength];
        System.arraycopy(a, 0, out, 0, Math.min(a.length, newLength));
        return out;
    }

    public static boolean[] copyOf(boolean[] a, int newLength) {
        boolean[] out = new boolean[newLength];
        System.arraycopy(a, 0, out, 0, Math.min(a.length, newLength));
        return out;
    }

    public static <T> T[] copyOf(T[] a, int newLength) {
        T[] out = (T[]) java.lang.reflect.Array.newInstance(
                a.getClass().getComponentType(), newLength);
        System.arraycopy(a, 0, out, 0, Math.min(a.length, newLength));
        return out;
    }

    public static <T, U> T[] copyOf(U[] original, int newLength, Class<? extends T[]> newType) {
        T[] copy = ((Object)newType == (Object)Object[].class)
            ? (T[]) new Object[newLength]
            : (T[]) java.lang.reflect.Array.newInstance(newType.getComponentType(), newLength);
        System.arraycopy(original, 0, copy, 0, Math.min(original.length, newLength));
        return copy;
    }

    public static int[] copyOfRange(int[] a, int from, int to) {
        int length = rangeLength(from, to);
        int[] out = new int[length];
        System.arraycopy(a, from, out, 0, Math.min(a.length - from, length));
        return out;
    }

    public static byte[] copyOfRange(byte[] a, int from, int to) {
        int length = rangeLength(from, to);
        byte[] out = new byte[length];
        System.arraycopy(a, from, out, 0, Math.min(a.length - from, length));
        return out;
    }

    public static char[] copyOfRange(char[] a, int from, int to) {
        int length = rangeLength(from, to);
        char[] out = new char[length];
        System.arraycopy(a, from, out, 0, Math.min(a.length - from, length));
        return out;
    }

    public static <T> T[] copyOfRange(T[] a, int from, int to) {
        int length = rangeLength(from, to);
        T[] out = (T[]) java.lang.reflect.Array.newInstance(
                a.getClass().getComponentType(), length);
        System.arraycopy(a, from, out, 0, Math.min(a.length - from, length));
        return out;
    }

    private static int rangeLength(int from, int to) {
        int length = to - from;
        if (length < 0) throw new IllegalArgumentException(from + " > " + to);
        return length;
    }

    public static boolean equals(int[] a, int[] b) {
        if (a == b) {
            return true;
        }
        if (a == null || b == null || a.length != b.length) {
            return false;
        }
        for (int i = 0; i < a.length; i++) {
            if (a[i] != b[i]) {
                return false;
            }
        }
        return true;
    }

    public static boolean equals(byte[] a, byte[] b) {
        if (a == b) {
            return true;
        }
        if (a == null || b == null || a.length != b.length) {
            return false;
        }
        for (int i = 0; i < a.length; i++) {
            if (a[i] != b[i]) {
                return false;
            }
        }
        return true;
    }

    public static boolean equals(char[] a, char[] b) {
        if (a == b) {
            return true;
        }
        if (a == null || b == null || a.length != b.length) {
            return false;
        }
        for (int i = 0; i < a.length; i++) {
            if (a[i] != b[i]) {
                return false;
            }
        }
        return true;
    }

    public static boolean equals(Object[] a, Object[] b) {
        if (a == b) {
            return true;
        }
        if (a == null || b == null || a.length != b.length) {
            return false;
        }
        for (int i = 0; i < a.length; i++) {
            if (a[i] == null ? b[i] != null : !a[i].equals(b[i])) {
                return false;
            }
        }
        return true;
    }

    public static int hashCode(int[] a) {
        if (a == null) {
            return 0;
        }
        int h = 1;
        for (int i = 0; i < a.length; i++) {
            h = 31 * h + a[i];
        }
        return h;
    }

    public static int hashCode(byte[] a) {
        if (a == null) return 0;
        int hash = 1;
        for (byte value : a) hash = 31 * hash + value;
        return hash;
    }

    public static int hashCode(Object[] a) {
        if (a == null) {
            return 0;
        }
        int h = 1;
        for (int i = 0; i < a.length; i++) {
            h = 31 * h + (a[i] == null ? 0 : a[i].hashCode());
        }
        return h;
    }

    public static void sort(int[] a) {
        insertionSortInt(a, 0, a.length);
    }

    public static void sort(int[] a, int fromIndex, int toIndex) {
        insertionSortInt(a, fromIndex, toIndex);
    }

    public static void sort(long[] a) {
        for (int i = 1; i < a.length; i++) {
            long v = a[i];
            int j = i - 1;
            while (j >= 0 && a[j] > v) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = v;
        }
    }

    public static void sort(char[] a) {
        for (int i = 1; i < a.length; i++) {
            char v = a[i];
            int j = i - 1;
            while (j >= 0 && a[j] > v) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = v;
        }
    }

    public static void sort(Object[] a) {
        if (a == null) throw new NullPointerException();
        sort(a, 0, a.length);
    }

    public static void sort(Object[] a, int fromIndex, int toIndex) {
        if (a == null) throw new NullPointerException();
        rangeCheck(a.length, fromIndex, toIndex);
        for (int i = fromIndex + 1; i < toIndex; i++) {
            Object v = a[i];
            int j = i - 1;
            while (j >= fromIndex && ((Comparable) a[j]).compareTo(v) > 0) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = v;
        }
    }

    public static <T> void sort(T[] a, Comparator<? super T> c) {
        if (a == null) throw new NullPointerException();
        sort(a, 0, a.length, c);
    }

    public static <T> void sort(T[] a, int fromIndex, int toIndex, Comparator<? super T> c) {
        if (a == null) throw new NullPointerException();
        rangeCheck(a.length, fromIndex, toIndex);
        if (c == null) {
            sort(a, fromIndex, toIndex);
            return;
        }
        for (int i = fromIndex + 1; i < toIndex; i++) {
            T v = a[i];
            int j = i - 1;
            while (j >= fromIndex && c.compare(a[j], v) > 0) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = v;
        }
    }

    public static void sort(byte[] a) {
        if (a == null) throw new NullPointerException();
        for (int i = 1; i < a.length; i++) {
            byte v = a[i];
            int j = i - 1;
            while (j >= 0 && a[j] > v) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = v;
        }
    }

    public static void sort(short[] a) {
        if (a == null) throw new NullPointerException();
        for (int i = 1; i < a.length; i++) {
            short v = a[i];
            int j = i - 1;
            while (j >= 0 && a[j] > v) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = v;
        }
    }

    public static void sort(float[] a) {
        if (a == null) throw new NullPointerException();
        for (int i = 1; i < a.length; i++) {
            float v = a[i];
            int j = i - 1;
            while (j >= 0 && a[j] > v) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = v;
        }
    }

    public static void sort(double[] a) {
        if (a == null) throw new NullPointerException();
        for (int i = 1; i < a.length; i++) {
            double v = a[i];
            int j = i - 1;
            while (j >= 0 && a[j] > v) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = v;
        }
    }

    public static int binarySearch(int[] a, int key) {
        int lo = 0;
        int hi = a.length - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < key) {
                lo = mid + 1;
            } else if (a[mid] > key) {
                hi = mid - 1;
            } else {
                return mid;
            }
        }
        return -(lo + 1);
    }

    public static int binarySearch(long[] a, long key) {
        int lo = 0;
        int hi = a.length - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < key) {
                lo = mid + 1;
            } else if (a[mid] > key) {
                hi = mid - 1;
            } else {
                return mid;
            }
        }
        return -(lo + 1);
    }

    public static int binarySearch(Object[] a, Object key) {
        return binarySearch(a, 0, a.length, key);
    }

    public static int binarySearch(Object[] a, int fromIndex, int toIndex, Object key) {
        rangeCheck(a.length, fromIndex, toIndex);
        int lo = fromIndex;
        int hi = toIndex - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int cmp = ((Comparable) a[mid]).compareTo(key);
            if (cmp < 0) {
                lo = mid + 1;
            } else if (cmp > 0) {
                hi = mid - 1;
            } else {
                return mid;
            }
        }
        return -(lo + 1);
    }

    public static <T> int binarySearch(T[] a, T key, Comparator<? super T> c) {
        return binarySearch(a, 0, a.length, key, c);
    }

    public static <T> int binarySearch(T[] a, int fromIndex, int toIndex, T key, Comparator<? super T> c) {
        rangeCheck(a.length, fromIndex, toIndex);
        if (c == null) return binarySearch(a, fromIndex, toIndex, key);
        int lo = fromIndex;
        int hi = toIndex - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int cmp = c.compare(a[mid], key);
            if (cmp < 0) {
                lo = mid + 1;
            } else if (cmp > 0) {
                hi = mid - 1;
            } else {
                return mid;
            }
        }
        return -(lo + 1);
    }

    public static int binarySearch(byte[] a, byte key) {
        int lo = 0, hi = a.length - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < key) lo = mid + 1;
            else if (a[mid] > key) hi = mid - 1;
            else return mid;
        }
        return -(lo + 1);
    }

    public static int binarySearch(char[] a, char key) {
        int lo = 0, hi = a.length - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < key) lo = mid + 1;
            else if (a[mid] > key) hi = mid - 1;
            else return mid;
        }
        return -(lo + 1);
    }

    public static int binarySearch(short[] a, short key) {
        int lo = 0, hi = a.length - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < key) lo = mid + 1;
            else if (a[mid] > key) hi = mid - 1;
            else return mid;
        }
        return -(lo + 1);
    }

    public static int binarySearch(float[] a, float key) {
        int lo = 0, hi = a.length - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < key) lo = mid + 1;
            else if (a[mid] > key) hi = mid - 1;
            else return mid;
        }
        return -(lo + 1);
    }

    public static int binarySearch(double[] a, double key) {
        int lo = 0, hi = a.length - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < key) lo = mid + 1;
            else if (a[mid] > key) hi = mid - 1;
            else return mid;
        }
        return -(lo + 1);
    }

    private static void rangeCheck(int arrayLength, int fromIndex, int toIndex) {
        if (fromIndex > toIndex) {
            throw new IllegalArgumentException("fromIndex(" + fromIndex + ") > toIndex(" + toIndex + ")");
        }
        if (fromIndex < 0) {
            throw new ArrayIndexOutOfBoundsException(fromIndex);
        }
        if (toIndex > arrayLength) {
            throw new ArrayIndexOutOfBoundsException(toIndex);
        }
    }

    private static void insertionSortInt(int[] a, int from, int to) {
        for (int i = from + 1; i < to; i++) {
            int v = a[i];
            int j = i - 1;
            while (j >= from && a[j] > v) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = v;
        }
    }

    public static String toString(boolean[] a) {
        if (a == null) return "null";
        int iMax = a.length - 1;
        if (iMax == -1) return "[]";
        StringBuilder b = new StringBuilder();
        b.append('[');
        for (int i = 0; ; i++) {
            b.append(a[i]);
            if (i == iMax) return b.append(']').toString();
            b.append(", ");
        }
    }

    public static String toString(byte[] a) {
        if (a == null) return "null";
        int iMax = a.length - 1;
        if (iMax == -1) return "[]";
        StringBuilder b = new StringBuilder();
        b.append('[');
        for (int i = 0; ; i++) {
            b.append(a[i]);
            if (i == iMax) return b.append(']').toString();
            b.append(", ");
        }
    }

    public static String toString(char[] a) {
        if (a == null) return "null";
        int iMax = a.length - 1;
        if (iMax == -1) return "[]";
        StringBuilder b = new StringBuilder();
        b.append('[');
        for (int i = 0; ; i++) {
            b.append(a[i]);
            if (i == iMax) return b.append(']').toString();
            b.append(", ");
        }
    }

    public static String toString(short[] a) {
        if (a == null) return "null";
        int iMax = a.length - 1;
        if (iMax == -1) return "[]";
        StringBuilder b = new StringBuilder();
        b.append('[');
        for (int i = 0; ; i++) {
            b.append(a[i]);
            if (i == iMax) return b.append(']').toString();
            b.append(", ");
        }
    }

    public static String toString(int[] a) {
        if (a == null) return "null";
        int iMax = a.length - 1;
        if (iMax == -1) return "[]";
        StringBuilder b = new StringBuilder();
        b.append('[');
        for (int i = 0; ; i++) {
            b.append(a[i]);
            if (i == iMax) return b.append(']').toString();
            b.append(", ");
        }
    }

    public static String toString(long[] a) {
        if (a == null) return "null";
        int iMax = a.length - 1;
        if (iMax == -1) return "[]";
        StringBuilder b = new StringBuilder();
        b.append('[');
        for (int i = 0; ; i++) {
            b.append(a[i]);
            if (i == iMax) return b.append(']').toString();
            b.append(", ");
        }
    }

    public static String toString(float[] a) {
        if (a == null) return "null";
        int iMax = a.length - 1;
        if (iMax == -1) return "[]";
        StringBuilder b = new StringBuilder();
        b.append('[');
        for (int i = 0; ; i++) {
            b.append(a[i]);
            if (i == iMax) return b.append(']').toString();
            b.append(", ");
        }
    }

    public static String toString(double[] a) {
        if (a == null) return "null";
        int iMax = a.length - 1;
        if (iMax == -1) return "[]";
        StringBuilder b = new StringBuilder();
        b.append('[');
        for (int i = 0; ; i++) {
            b.append(a[i]);
            if (i == iMax) return b.append(']').toString();
            b.append(", ");
        }
    }

    public static String toString(Object[] a) {
        if (a == null) return "null";
        int iMax = a.length - 1;
        if (iMax == -1) return "[]";
        StringBuilder b = new StringBuilder();
        b.append('[');
        for (int i = 0; ; i++) {
            b.append(String.valueOf(a[i]));
            if (i == iMax) return b.append(']').toString();
            b.append(", ");
        }
    }

    public static String deepToString(Object[] a) {
        return toString(a);
    }
}
