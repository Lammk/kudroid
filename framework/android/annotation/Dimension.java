package android.annotation;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

@Retention(RetentionPolicy.SOURCE)
public @interface Dimension {
    int PX = 0;
    int DP = 1;
    int SP = 2;

    int unit() default PX;
}
