package java.util;

public final class Collections {

    public static final List EMPTY_LIST = new EmptyList<Object>();
    public static final Set EMPTY_SET = new EmptySet<Object>();
    public static final Map EMPTY_MAP = new EmptyMap<Object, Object>();
    private static final ListIterator EMPTY_ITERATOR = new EmptyListIterator<Object>();

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

    public static <T extends Object & Comparable<? super T>> T max(Collection<? extends T> values) {
        Iterator<? extends T> iterator = values.iterator();
        T result = iterator.next();
        while (iterator.hasNext()) {
            T value = iterator.next();
            if (value.compareTo(result) > 0) result = value;
        }
        return result;
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    public static <T> T max(Collection<? extends T> values, Comparator<? super T> comparator) {
        if (comparator == null) return (T) max((Collection) values);
        Iterator<? extends T> iterator = values.iterator();
        T result = iterator.next();
        while (iterator.hasNext()) {
            T value = iterator.next();
            if (comparator.compare(value, result) > 0) result = value;
        }
        return result;
    }

    public static <T extends Object & Comparable<? super T>> T min(Collection<? extends T> values) {
        Iterator<? extends T> iterator = values.iterator();
        T result = iterator.next();
        while (iterator.hasNext()) {
            T value = iterator.next();
            if (value.compareTo(result) < 0) result = value;
        }
        return result;
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    public static <T> T min(Collection<? extends T> values, Comparator<? super T> comparator) {
        if (comparator == null) return (T) min((Collection) values);
        Iterator<? extends T> iterator = values.iterator();
        T result = iterator.next();
        while (iterator.hasNext()) {
            T value = iterator.next();
            if (comparator.compare(value, result) < 0) result = value;
        }
        return result;
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

    @SuppressWarnings("unchecked")
    public static <T> List<T> emptyList() {
        return (List<T>) EMPTY_LIST;
    }

    @SuppressWarnings("unchecked")
    public static <K, V> Map<K, V> emptyMap() {
        return (Map<K, V>) EMPTY_MAP;
    }

    @SuppressWarnings("unchecked")
    public static <T> Set<T> emptySet() {
        return (Set<T>) EMPTY_SET;
    }

    public static <T> Iterator<T> emptyIterator() { return Collections.<T>emptyListIterator(); }

    @SuppressWarnings("unchecked")
    public static <T> ListIterator<T> emptyListIterator() { return (ListIterator<T>) EMPTY_ITERATOR; }

    private static final class EmptyListIterator<E> implements ListIterator<E> {
        public boolean hasNext() { return false; }
        public E next() { throw new NoSuchElementException(); }
        public boolean hasPrevious() { return false; }
        public E previous() { throw new NoSuchElementException(); }
        public int nextIndex() { return 0; }
        public int previousIndex() { return -1; }
        public void remove() { throw new IllegalStateException(); }
        public void set(E value) { throw new IllegalStateException(); }
        public void add(E value) { throw new UnsupportedOperationException(); }
    }

    private static final class EmptyList<E> extends AbstractList<E>
            implements RandomAccess, java.io.Serializable {
        public int size() { return 0; }
        public E get(int index) { throw new IndexOutOfBoundsException(); }
        public Iterator<E> iterator() { return Collections.<E>emptyIterator(); }
        public ListIterator<E> listIterator() { return Collections.<E>emptyListIterator(); }
        public ListIterator<E> listIterator(int index) {
            if (index != 0) throw new IndexOutOfBoundsException();
            return listIterator();
        }
        public List<E> subList(int from, int to) {
            if (from < 0 || to > 0) throw new IndexOutOfBoundsException();
            if (from > to) throw new IllegalArgumentException();
            return this;
        }
        public boolean equals(Object other) { return other instanceof List && ((List<?>) other).isEmpty(); }
        public int hashCode() { return 1; }
        public String toString() { return "[]"; }
        private Object readResolve() { return EMPTY_LIST; }
    }

    private static final class EmptySet<E> extends AbstractSet<E> implements java.io.Serializable {
        public int size() { return 0; }
        public Iterator<E> iterator() { return Collections.<E>emptyIterator(); }
        public String toString() { return "[]"; }
        private Object readResolve() { return EMPTY_SET; }
    }

    private static final class EmptyMap<K, V> extends AbstractMap<K, V> implements java.io.Serializable {
        public Set<K> keySet() { return Collections.<K>emptySet(); }
        public Collection<V> values() { return Collections.<V>emptyList(); }
        public Set<Entry<K, V>> entrySet() { return Collections.<Entry<K, V>>emptySet(); }
        public boolean equals(Object other) { return other instanceof Map && ((Map<?, ?>) other).isEmpty(); }
        public int hashCode() { return 0; }
        public String toString() { return "{}"; }
        private Object readResolve() { return EMPTY_MAP; }
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

    public static <E> Set<E> newSetFromMap(Map<E, Boolean> map) {
        if (map == null) throw new NullPointerException("map == null");
        return map.keySet();
    }

    public static <K, V> Map<K, V> singletonMap(K key, V value) {
        HashMap<K, V> out = new HashMap<K, V>(1);
        out.put(key, value);
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

    public static <T> Collection<T> synchronizedCollection(Collection<T> collection) {
        return new SynchronizedCollection<T>(collection);
    }

    public static <T> List<T> synchronizedList(List<T> list) {
        return list instanceof RandomAccess ? new SynchronizedRandomAccessList<T>(list)
                : new SynchronizedList<T>(list);
    }

    public static <K, V> Map<K, V> synchronizedMap(Map<K, V> map) {
        return new SynchronizedMap<K, V>(map);
    }

    public static <T> Set<T> synchronizedSet(Set<T> set) {
        return new SynchronizedSet<T>(set);
    }

    private static class SynchronizedCollection<E> implements Collection<E>, java.io.Serializable {
        final Collection<E> collection;
        final Object mutex;
        SynchronizedCollection(Collection<E> collection) {
            if (collection == null) throw new NullPointerException();
            this.collection = collection;
            mutex = this;
        }
        SynchronizedCollection(Collection<E> collection, Object mutex) {
            if (collection == null) throw new NullPointerException();
            this.collection = collection;
            this.mutex = mutex;
        }
        public int size() { synchronized (mutex) { return collection.size(); } }
        public boolean isEmpty() { synchronized (mutex) { return collection.isEmpty(); } }
        public boolean contains(Object value) { synchronized (mutex) { return collection.contains(value); } }
        public Object[] toArray() { synchronized (mutex) { return collection.toArray(); } }
        public <T> T[] toArray(T[] array) { synchronized (mutex) { return collection.toArray(array); } }
        public boolean add(E value) { synchronized (mutex) { return collection.add(value); } }
        public boolean remove(Object value) { synchronized (mutex) { return collection.remove(value); } }
        public boolean containsAll(Collection<?> values) { synchronized (mutex) { return collection.containsAll(values); } }
        public boolean addAll(Collection<? extends E> values) { synchronized (mutex) { return collection.addAll(values); } }
        public boolean removeAll(Collection<?> values) { synchronized (mutex) { return collection.removeAll(values); } }
        public boolean retainAll(Collection<?> values) { synchronized (mutex) { return collection.retainAll(values); } }
        public void clear() { synchronized (mutex) { collection.clear(); } }
        public String toString() { synchronized (mutex) { return collection.toString(); } }
        // Iteration requires the caller to hold the wrapper's monitor.
        public Iterator<E> iterator() { return collection.iterator(); }
    }

    private static final class SynchronizedSet<E> extends SynchronizedCollection<E> implements Set<E> {
        SynchronizedSet(Set<E> set) { super(set); }
        SynchronizedSet(Set<E> set, Object mutex) { super(set, mutex); }
        public boolean equals(Object other) {
            if (other == this) return true;
            synchronized (mutex) { return collection.equals(other); }
        }
        public int hashCode() { synchronized (mutex) { return collection.hashCode(); } }
    }

    private static class SynchronizedList<E> extends SynchronizedCollection<E> implements List<E> {
        final List<E> list;
        SynchronizedList(List<E> list) { super(list); this.list = list; }
        SynchronizedList(List<E> list, Object mutex) { super(list, mutex); this.list = list; }
        public E get(int index) { synchronized (mutex) { return list.get(index); } }
        public E set(int index, E value) { synchronized (mutex) { return list.set(index, value); } }
        public void add(int index, E value) { synchronized (mutex) { list.add(index, value); } }
        public E remove(int index) { synchronized (mutex) { return list.remove(index); } }
        public int indexOf(Object value) { synchronized (mutex) { return list.indexOf(value); } }
        public int lastIndexOf(Object value) { synchronized (mutex) { return list.lastIndexOf(value); } }
        public ListIterator<E> listIterator() { return list.listIterator(); }
        public List<E> subList(int from, int to) {
            synchronized (mutex) { return new SynchronizedList<E>(list.subList(from, to), mutex); }
        }
        public boolean equals(Object other) {
            if (other == this) return true;
            synchronized (mutex) { return list.equals(other); }
        }
        public int hashCode() { synchronized (mutex) { return list.hashCode(); } }
    }

    private static final class SynchronizedRandomAccessList<E> extends SynchronizedList<E> implements RandomAccess {
        SynchronizedRandomAccessList(List<E> list) { super(list); }
        SynchronizedRandomAccessList(List<E> list, Object mutex) { super(list, mutex); }
        public List<E> subList(int from, int to) {
            synchronized (mutex) { return new SynchronizedRandomAccessList<E>(list.subList(from, to), mutex); }
        }
    }

    private static final class SynchronizedMap<K, V> implements Map<K, V>, java.io.Serializable {
        private final Map<K, V> map;
        private final Object mutex = this;
        private Set<K> keys;
        private Collection<V> values;
        private Set<Entry<K, V>> entries;
        SynchronizedMap(Map<K, V> map) {
            if (map == null) throw new NullPointerException();
            this.map = map;
        }
        public int size() { synchronized (mutex) { return map.size(); } }
        public boolean isEmpty() { synchronized (mutex) { return map.isEmpty(); } }
        public boolean containsKey(Object key) { synchronized (mutex) { return map.containsKey(key); } }
        public boolean containsValue(Object value) { synchronized (mutex) { return map.containsValue(value); } }
        public V get(Object key) { synchronized (mutex) { return map.get(key); } }
        public V put(K key, V value) { synchronized (mutex) { return map.put(key, value); } }
        public V remove(Object key) { synchronized (mutex) { return map.remove(key); } }
        public void putAll(Map<? extends K, ? extends V> values) { synchronized (mutex) { map.putAll(values); } }
        public void clear() { synchronized (mutex) { map.clear(); } }
        public Set<K> keySet() {
            synchronized (mutex) {
                if (keys == null) keys = new SynchronizedSet<K>(map.keySet(), mutex);
                return keys;
            }
        }
        public Collection<V> values() {
            synchronized (mutex) {
                if (values == null) values = new SynchronizedCollection<V>(map.values(), mutex);
                return values;
            }
        }
        public Set<Entry<K, V>> entrySet() {
            synchronized (mutex) {
                if (entries == null) entries = new SynchronizedSet<Entry<K, V>>(map.entrySet(), mutex);
                return entries;
            }
        }
        public boolean equals(Object other) {
            if (other == this) return true;
            synchronized (mutex) { return map.equals(other); }
        }
        public int hashCode() { synchronized (mutex) { return map.hashCode(); } }
        public String toString() { synchronized (mutex) { return map.toString(); } }
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
