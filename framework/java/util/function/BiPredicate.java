package java.util.function;
@FunctionalInterface
public interface BiPredicate<T, U> {
    boolean test(T t, U u);
}
