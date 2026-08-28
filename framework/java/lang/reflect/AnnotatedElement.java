package java.lang.reflect;
import java.lang.annotation.Annotation;
public interface AnnotatedElement {
    default boolean isAnnotationPresent(Class<? extends Annotation> annotationClass) {
        return getAnnotation(annotationClass) != null;
    }
    <T extends Annotation> T getAnnotation(Class<T> annotationClass);
    Annotation[] getAnnotations();
    Annotation[] getDeclaredAnnotations();
}
