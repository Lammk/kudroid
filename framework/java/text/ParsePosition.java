package java.text;

/**
 * java.text.ParsePosition — cursor and error index for incremental parsing.
 */
public class ParsePosition {
    private int currentPosition;
    private int errorIndex = -1;

    public ParsePosition(int index) {
        currentPosition = index;
    }

    public int getIndex() {
        return currentPosition;
    }

    public void setIndex(int index) {
        currentPosition = index;
    }

    public int getErrorIndex() {
        return errorIndex;
    }

    public void setErrorIndex(int index) {
        errorIndex = index;
    }

    @Override
    public boolean equals(Object object) {
        if (!(object instanceof ParsePosition)) return false;
        ParsePosition other = (ParsePosition) object;
        return currentPosition == other.currentPosition && errorIndex == other.errorIndex;
    }

    @Override
    public int hashCode() {
        return currentPosition + errorIndex;
    }

    @Override
    public String toString() {
        return "java.text.ParsePosition[index=" + currentPosition
                + ",errorIndex=" + errorIndex + "]";
    }
}
