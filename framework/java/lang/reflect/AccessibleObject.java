package java.lang.reflect;

/**
 * java.lang.reflect.AccessibleObject — the common base of Field, Method and Constructor.
 *
 * Was an auto-generated empty stub, and all five real APKs in the corpus reference it. They
 * do not reference it for its own sake: the pattern is
 *
 *   AccessibleObject.setAccessible(new AccessibleObject[] { field }, true);
 *
 * or a variable typed as AccessibleObject holding a Field or a Method. Both need the class
 * to be real AND to be the superclass of the three, which the stub was not — so even code
 * that loaded would have failed the cast.
 *
 * Accessibility is not enforced anywhere in KuART: there is no module system and no security
 * manager, so every member is reachable. The methods below therefore report "accessible" and
 * accept every request. That is not a shortcut standing in for a check — with nothing to
 * enforce, refusing would be the inaccurate answer.
 */
public class AccessibleObject implements AnnotatedElement {
    protected AccessibleObject() {}

    /**
     * The bulk form, which is why this class appears in the corpus at all.
     *
     * Static and taking an array, unlike the instance method below. Libraries that unlock
     * several members at once call it and never touch AccessibleObject otherwise.
     */
    public static void setAccessible(AccessibleObject[] array, boolean flag) {
        if (array == null) return;
        for (AccessibleObject object : array) {
            if (object != null) object.setAccessible(flag);
        }
    }

    public void setAccessible(boolean flag) {
    }

    /**
     * True, because nothing in KuART restricts access.
     *
     * Reporting false would send callers into their setAccessible path and then into failure
     * handling for a restriction that does not exist.
     */
    public boolean isAccessible() {
        return true;
    }

    public boolean canAccess(Object obj) {
        return true;
    }

    public boolean trySetAccessible() {
        return true;
    }

    // AnnotatedElement: no annotations retained, so return empty arrays.

    @Override
    public <T extends java.lang.annotation.Annotation> T getAnnotation(Class<T> annotationClass) {
        return null;
    }

    @Override
    public java.lang.annotation.Annotation[] getAnnotations() {
        return new java.lang.annotation.Annotation[0];
    }

    @Override
    public java.lang.annotation.Annotation[] getDeclaredAnnotations() {
        return new java.lang.annotation.Annotation[0];
    }

    @Override
    public boolean isAnnotationPresent(
            Class<? extends java.lang.annotation.Annotation> annotationClass) {
        return false;
    }
}
