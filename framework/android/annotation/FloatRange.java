package android.annotation;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

@Retention(RetentionPolicy.SOURCE)
public @interface FloatRange {
    double from() default Double.NEGATIVE_INFINITY;
    double to() default Double.POSITIVE_INFINITY;
    boolean fromInclusive() default true;
    boolean toInclusive() default true;
}
