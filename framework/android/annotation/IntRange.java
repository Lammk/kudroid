package android.annotation;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

@Retention(RetentionPolicy.SOURCE)
public @interface IntRange {
    long from() default Long.MIN_VALUE;
    long to() default Long.MAX_VALUE;
}
