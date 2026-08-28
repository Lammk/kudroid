package android.annotation;
import java.lang.annotation.*;
@Retention(RetentionPolicy.SOURCE)
@Target({ElementType.ANNOTATION_TYPE})
public @interface IntDef {
    int[] value() default {};
    boolean flag() default false;
}
