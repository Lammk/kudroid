package java.util;

public final class Collections {

    private Collections() {
    }

    public static <T> void sort(List<T> list, Comparator<? super T> c) {
        int n = list.size();
        // Insertion sort: n is small in all cases used by the framework.
        for (int i = 1; i < n; i++) {
            T v = list.get(i);
            int j = i - 1;
            while (j >= 0 && c.compare(list.get(j), v) > 0) {
                list.set(j + 1, list.get(j));
                j--;
            }
            list.set(j + 1, v);
        }
    }

    public static <T extends Comparable<? super T>> void sort(List<T> list) {
        sort(list, new NaturalOrder<T>());
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    public static void swap(List<?> list, int i, int j) {
        final List l = list;
        l.set(i, l.set(j, l.get(i)));
    }

    public static void reverse(List<?> list) {
        int n = list.size();
        List<Object> l = (List<Object>) list;
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            Object t = l.get(i);
            l.set(i, l.get(j));
            l.set(j, t);
        }
    }

    public static <T> List<T> emptyList() {
        return new ArrayList<T>(1);
    }

    public static <K, V> Map<K, V> emptyMap() {
        return new HashMap<K, V>(1);
    }

    public static <T> Set<T> emptySet() {
        return new HashSet<T>(1);
    }

    public static <T> Enumeration<T> emptyEnumeration() {
        return new Enumeration<T>() {
            public boolean hasMoreElements() { return false; }
            public T nextElement() { throw new NoSuchElementException(); }
        };
    }

    public static <T> Enumeration<T> enumeration(final Collection<T> c) {
        return new Enumeration<T>() {
            private final Iterator<T> i = c.iterator();
            public boolean hasMoreElements() { return i.hasNext(); }
            public T nextElement() { return i.next(); }
        };
    }

    public static <T> List<T> singletonList(T item) {
        ArrayList<T> out = new ArrayList<T>(1);
        out.add(item);
        return out;
    }

    public static <T> Set<T> singleton(T item) {
        HashSet<T> out = new HashSet<T>(1);
        out.add(item);
        return out;
    }

    public static <T> List<T> unmodifiableList(List<T> list) {
        return list;
    }

    public static <T> Set<T> unmodifiableSet(Set<T> set) {
        return set;
    }

    public static <K, V> Map<K, V> unmodifiableMap(Map<K, V> map) {
        return map;
    }

    public static <T> Collection<T> unmodifiableCollection(Collection<T> c) {
        return c;
    }

    public static <T> List<T> synchronizedList(List<T> list) {
        return list;
    }

    public static <K, V> Map<K, V> synchronizedMap(Map<K, V> map) {
        return map;
    }

    public static <T> Set<T> synchronizedSet(Set<T> set) {
        return set;
    }

    public static <T> void addAll(Collection<T> c, T... items) {
        for (int i = 0; i < items.length; i++) {
            c.add(items[i]);
        }
    }

    public static <T> void fill(List<T> list, T value) {
        for (int i = 0; i < list.size(); i++) {
            list.set(i, value);
        }
    }

    private static final class NaturalOrder<T extends Comparable<? super T>>
            implements Comparator<T> {

        public int compare(T a, T b) {
            return a.compareTo(b);
        }
    }
}
