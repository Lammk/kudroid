package java.util;

public class StringTokenizer implements Enumeration<Object> {
    private int currentPosition;
    private int newPosition;
    private int maxPosition;
    private String str;
    private String delimiters;
    private boolean retDelims;
    private boolean delimsChanged;

    public StringTokenizer(String str, String delim, boolean returnDelims) {
        currentPosition = 0;
        newPosition = -1;
        delimsChanged = false;
        this.str = str;
        maxPosition = str.length();
        this.delimiters = delim;
        this.retDelims = returnDelims;
    }

    public StringTokenizer(String str, String delim) {
        this(str, delim, false);
    }

    public StringTokenizer(String str) {
        this(str, " \t\n\r\f", false);
    }

    private int skipDelimiters(int startPos) {
        if (delimiters == null) throw new NullPointerException();
        int position = startPos;
        while (!retDelims && position < maxPosition) {
            char c = str.charAt(position);
            if (delimiters.indexOf(c) < 0) break;
            position++;
        }
        return position;
    }

    private int scanToken(int startPos) {
        int position = startPos;
        while (position < maxPosition) {
            char c = str.charAt(position);
            if (delimiters.indexOf(c) >= 0) break;
            position++;
        }
        if (retDelims && (startPos == position)) {
            char c = str.charAt(position);
            if (delimiters.indexOf(c) >= 0) position++;
        }
        return position;
    }

    public boolean hasMoreTokens() {
        newPosition = skipDelimiters(currentPosition);
        return (newPosition < maxPosition);
    }

    public String nextToken() {
        currentPosition = (newPosition >= 0 && !delimsChanged) ?
            newPosition : skipDelimiters(currentPosition);
        delimsChanged = false;
        newPosition = -1;

        if (currentPosition >= maxPosition) throw new NoSuchElementException();
        int start = currentPosition;
        currentPosition = scanToken(currentPosition);
        return str.substring(start, currentPosition);
    }

    public String nextToken(String delim) {
        delimiters = delim;
        delimsChanged = true;
        return nextToken();
    }

    public boolean hasMoreElements() { return hasMoreTokens(); }
    public Object nextElement() { return nextToken(); }
    public int countTokens() {
        int count = 0;
        int currpos = currentPosition;
        while (currpos < maxPosition) {
            currpos = skipDelimiters(currpos);
            if (currpos >= maxPosition) break;
            currpos = scanToken(currpos);
            count++;
        }
        return count;
    }
}
