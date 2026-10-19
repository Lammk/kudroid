package java.util.concurrent.atomic;

import java.lang.reflect.Field;
import java.lang.reflect.Modifier;

final class FieldUpdaterSupport {
    static Field find(Class<?> owner, Class<?> type, String name) {
        if (owner == null || type == null || name == null) throw new NullPointerException();
        try {
            Field field = owner.getDeclaredField(name);
            if (field.getType() != type) throw new ClassCastException("Wrong field type");
            int flags = field.getModifiers();
            if (!Modifier.isVolatile(flags) || Modifier.isStatic(flags) || Modifier.isFinal(flags)) {
                throw new IllegalArgumentException("Field must be a volatile instance field");
            }
            field.setAccessible(true);
            return field;
        } catch (NoSuchFieldException e) {
            throw new RuntimeException(e);
        }
    }

    static void check(Class<?> owner, Object object) {
        if (!owner.isInstance(object)) throw new ClassCastException();
    }
}
