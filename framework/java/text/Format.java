package java.text;

/**
 * java.text.Format — base class for the text formatters.
 */
public abstract class Format implements java.io.Serializable, Cloneable {

    protected Format() {
    }

    public final String format(Object object) {
        return format(object, new StringBuffer(), new FieldPosition(0)).toString();
    }

    public abstract StringBuffer format(Object object, StringBuffer buffer,
                                        FieldPosition field);

    public Object parseObject(String string) throws ParseException {
        ParsePosition position = new ParsePosition(0);
        Object result = parseObject(string, position);
        if (position.getIndex() == 0) {
            throw new ParseException("Unparseable: " + string, position.getErrorIndex());
        }
        return result;
    }

    public abstract Object parseObject(String string, ParsePosition position);

    @Override
    public Object clone() {
        try {
            return super.clone();
        } catch (CloneNotSupportedException e) {
            return null;
        }
    }

    /** Identifies a field within formatted output. */
    public static class Field {
        private final String name;

        protected Field(String fieldName) {
            name = fieldName;
        }

        public String getName() {
            return name;
        }

        @Override
        public String toString() {
            return getClass().getName() + "(" + name + ")";
        }
    }
}
