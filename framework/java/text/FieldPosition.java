package java.text;

/**
 * java.text.FieldPosition — which field a formatter wrote, and where.
 *
 * Formatters report the span of one field of interest so callers can highlight it.
 * KuDroid's formatters fill in begin/end when it is cheap and leave them at zero
 * otherwise, which is what the JDK does for fields that were not requested.
 */
public class FieldPosition {
    private int myField;
    private int beginIndex;
    private int endIndex;
    private Format.Field myAttribute;

    public FieldPosition(int field) {
        myField = field;
    }

    public FieldPosition(Format.Field attribute) {
        myAttribute = attribute;
        myField = -1;
    }

    public FieldPosition(Format.Field attribute, int field) {
        myAttribute = attribute;
        myField = field;
    }

    public int getField() {
        return myField;
    }

    public Format.Field getFieldAttribute() {
        return myAttribute;
    }

    public int getBeginIndex() {
        return beginIndex;
    }

    public void setBeginIndex(int index) {
        beginIndex = index;
    }

    public int getEndIndex() {
        return endIndex;
    }

    public void setEndIndex(int index) {
        endIndex = index;
    }

    @Override
    public boolean equals(Object object) {
        if (!(object instanceof FieldPosition)) return false;
        FieldPosition other = (FieldPosition) object;
        if (myAttribute == null ? other.myAttribute != null
                                : !myAttribute.equals(other.myAttribute)) {
            return false;
        }
        return myField == other.myField
                && beginIndex == other.beginIndex
                && endIndex == other.endIndex;
    }

    @Override
    public int hashCode() {
        int result = myAttribute == null ? 0 : myAttribute.hashCode();
        return result + myField + beginIndex + endIndex;
    }

    @Override
    public String toString() {
        return "java.text.FieldPosition[field=" + myField
                + ",attribute=" + myAttribute
                + ",beginIndex=" + beginIndex
                + ",endIndex=" + endIndex + "]";
    }
}
