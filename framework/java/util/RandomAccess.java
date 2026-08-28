package java.util;

/**
 * java.util.RandomAccess — marker interface: this List supports fast indexed access.
 *
 * Purely a tag with no methods, but Collections and ArrayList reference it to pick
 * between index-based and iterator-based algorithms.
 */
public interface RandomAccess {
}
