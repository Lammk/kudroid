package java.security;

public final class AccessController {
    private AccessController() {}

    public static <T> T doPrivileged(PrivilegedAction<T> action) { return action.run(); }
    public static <T> T doPrivileged(PrivilegedExceptionAction<T> action)
            throws PrivilegedActionException {
        try {
            return action.run();
        } catch (RuntimeException exception) {
            throw exception;
        } catch (Exception exception) {
            throw new PrivilegedActionException(exception);
        }
    }
    public static <T> T doPrivilegedWithCombiner(PrivilegedAction<T> action) {
        return doPrivileged(action);
    }
    public static <T> T doPrivilegedWithCombiner(PrivilegedExceptionAction<T> action)
            throws PrivilegedActionException {
        return doPrivileged(action);
    }
}
