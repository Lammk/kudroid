import java.util.*;

public class OrderedCollectionTests {
    private static int checks;
    private static void check(boolean value, String message) {
        ++checks;
        if (!value) throw new AssertionError(message);
    }
    private static void order(Collection<Integer> collection, int... expected) {
        Object[] array = collection.toArray();
        check(array.length == expected.length, "array size");
        for (int i = 0; i < expected.length; ++i) check(((Integer) array[i]).intValue() == expected[i], "iteration order");
    }
    public static int run() {
        checks = 0;
        TreeSet<Integer> set = new TreeSet<Integer>();
        set.add(7); set.add(1); set.add(4); set.add(1);
        order(set, 1, 4, 7);
        check(set.first() == 1 && set.last() == 7, "sorted endpoints");
        Iterator<Integer> it = set.iterator();
        check(it.next() == 1, "first iterator element");
        it.remove(); order(set, 4, 7);
        try { it.remove(); check(false, "double remove accepted"); }
        catch (IllegalStateException expected) { check(true, "double remove rejected"); }
        check(it.next() == 4 && it.next() == 7 && !it.hasNext(), "iterator survives its own removal");
        try { it.next(); check(false, "exhausted iterator accepted"); }
        catch (NoSuchElementException expected) { check(true, "exhausted iterator rejected"); }
        order(set.descendingSet(), 7, 4);
        it = set.descendingIterator(); check(it.next() == 7, "descending iterator starts at last");
        it.remove(); check(it.next() == 4, "descending removal preserves successor");
        set.add(1); set.add(7);
        check(set.equals(new TreeSet<Integer>(set)), "set equality");
        check(set.hashCode() == 12, "set hash code");
        NavigableSet<Integer> reverse = set.descendingSet();
        check(reverse.lower(4) == 7 && reverse.higher(4) == 1, "descending navigation");
        check(reverse.floor(5) == 7 && reverse.ceiling(5) == 4, "descending floor and ceiling");
        order(reverse.subSet(7, true, 1, false), 7, 4);
        order(reverse.headSet(4, true), 7, 4);
        order(reverse.tailSet(4, true), 4, 1);
        check(reverse.pollFirst() == 7 && set.contains(7) == false, "descending poll changes root");
        check(set.pollFirst() == 1 && set.pollLast() == 4 && set.pollFirst() == null, "poll endpoints and empty");
        for (int i = 20; i >= 0; --i) set.add(i);
        it = set.iterator(); set.add(21);
        try { it.next(); check(false, "modified iterator accepted"); }
        catch (ConcurrentModificationException expected) { check(true, "structural modification detected"); }
        TreeMap<Integer,String> map = new TreeMap<Integer,String>();
        for (int i = 20; i >= 0; --i) map.put(i, "v" + i);
        NavigableMap<Integer,String> range = map.subMap(4, false, 16, false);
        order(range.navigableKeySet(), 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        check(range.firstKey() == 5 && range.lastKey() == 15, "range endpoints");
        check(range.get(4) == null && range.get(16) == null, "range excludes endpoints");
        check(range.floorKey(100) == 15 && range.ceilingKey(-1) == 5, "range navigation clamps bounds");
        check(range.lowerKey(5) == null && range.higherKey(15) == null, "range navigation outside bounds");
        try { range.put(4, "x"); check(false, "excluded key inserted"); }
        catch (IllegalArgumentException expected) { check(true, "out-of-range insertion rejected"); }
        try { range.headMap(16, true); check(false, "excluded upper endpoint widened"); }
        catch (IllegalArgumentException expected) { check(true, "nested range cannot widen inclusion"); }
        check(range.headMap(16, false).size() == range.size(), "excluded parent boundary can remain excluded");
        check(range.subMap(4, false, 4, false).isEmpty(), "empty nested boundary range");
        Map.Entry<Integer,String> first = range.entrySet().iterator().next();
        check("v5".equals(first.setValue("changed")) && "changed".equals(map.get(5)), "entry value updates map");
        map.put(5, "again"); check("again".equals(first.getValue()), "entry observes value replacement");
        Map.Entry<Integer,String> snapshot = range.firstEntry();
        map.put(5, "later"); check("again".equals(snapshot.getValue()), "navigation returns snapshot entry");
        try { snapshot.setValue("x"); check(false, "navigation entry mutable"); }
        catch (UnsupportedOperationException expected) { check(true, "navigation entry is immutable"); }
        Iterator<String> values = range.values().iterator(); values.next(); values.remove();
        check(!map.containsKey(5), "value iterator removes from root");
        map.put(6, null); check(range.navigableKeySet().remove(6) && !map.containsKey(6), "key view removes null-valued mapping");
        map.put(6, "restored"); check(range.containsKey(6), "view observes root insertions");
        range.descendingMap().subMap(14, true, 12, true).clear();
        check(!map.containsKey(12) && !map.containsKey(14) && map.containsKey(11), "nested descending clear is bounded");
        TreeMap<Integer,String> copy = (TreeMap<Integer,String>) map.clone();
        copy.clear(); check(!map.isEmpty(), "map clone has independent storage");
        Comparator<String> caseInsensitive = new Comparator<String>() {
            public int compare(String a, String b) { return a.compareToIgnoreCase(b); }
        };
        TreeMap<String,Integer> custom = new TreeMap<String,Integer>(caseInsensitive);
        custom.put("beta", 1); custom.put("Alpha", 2); custom.put("ALPHA", 3);
        check(custom.size() == 2 && custom.get("alpha") == 3, "comparator defines key identity");
        check("Alpha".equals(custom.firstKey()) && "beta".equals(custom.lastKey()), "comparator ordering and original key retained");
        Comparator<Integer> nullFirst = new Comparator<Integer>() {
            public int compare(Integer a, Integer b) { return a == b ? 0 : a == null ? -1 : b == null ? 1 : a.compareTo(b); }
        };
        TreeMap<Integer,String> nullable = new TreeMap<Integer,String>(nullFirst);
        nullable.put(1, "one"); nullable.put(null, "null");
        check(nullable.firstKey() == null && "null".equals(nullable.get(null)), "comparator can support null keys");
        try { map.put(null, "x"); check(false, "natural ordering accepted null"); }
        catch (NullPointerException expected) { check(true, "natural ordering rejects null keys"); }
        TreeMap<Object,String> untyped = new TreeMap<Object,String>();
        try { untyped.put(new Object(), "x"); check(false, "noncomparable key accepted"); }
        catch (ClassCastException expected) { check(true, "noncomparable key rejected even on empty map"); }
        TreeMap<Integer,Integer> grid = new TreeMap<Integer,Integer>();
        for (int i = 0; i <= 20; i += 2) grid.put(i, i);
        for (boolean descending : new boolean[]{false, true}) {
            NavigableMap<Integer,Integer> direction = descending ? grid.descendingMap() : grid;
            for (boolean lowerInclusive : new boolean[]{false, true}) {
                for (boolean upperInclusive : new boolean[]{false, true}) {
                    NavigableMap<Integer,Integer> view = descending
                        ? direction.subMap(16, upperInclusive, 4, lowerInclusive)
                        : direction.subMap(4, lowerInclusive, 16, upperInclusive);
                    for (int query = -1; query <= 21; ++query) {
                        Integer lower = null, floor = null, ceiling = null, higher = null;
                        for (Integer key : view.navigableKeySet()) {
                            int compare = descending ? query - key : key - query;
                            if (compare < 0) lower = key;
                            if (compare <= 0) floor = key;
                            if (compare >= 0 && ceiling == null) ceiling = key;
                            if (compare > 0 && higher == null) higher = key;
                        }
                        check(Objects.equals(lower, view.lowerKey(query)), "lower matches ordered scan");
                        check(Objects.equals(floor, view.floorKey(query)), "floor matches ordered scan");
                        check(Objects.equals(ceiling, view.ceilingKey(query)), "ceiling matches ordered scan");
                        check(Objects.equals(higher, view.higherKey(query)), "higher matches ordered scan");
                    }
                }
            }
        }
        return checks;
    }
}
