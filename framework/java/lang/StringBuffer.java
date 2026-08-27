package java.lang;

public final class StringBuffer implements CharSequence, Appendable {

    private final StringBuilder delegate;

    public StringBuffer() {
        delegate = new StringBuilder();
    }

    public StringBuffer(int capacity) {
        delegate = new StringBuilder(capacity);
    }

    public StringBuffer(String str) {
        delegate = new StringBuilder(str);
    }

    public synchronized int length() {
        return delegate.length();
    }

    public synchronized char charAt(int index) {
        return delegate.charAt(index);
    }

    public synchronized void setCharAt(int index, char c) {
        delegate.setCharAt(index, c);
    }

    public synchronized void setLength(int newLength) {
        delegate.setLength(newLength);
    }

    public synchronized StringBuffer append(char c) {
        delegate.append(c);
        return this;
    }

    public synchronized StringBuffer append(String str) {
        delegate.append(str);
        return this;
    }

    public synchronized StringBuffer append(CharSequence csq) {
        delegate.append(csq);
        return this;
    }

    public synchronized StringBuffer append(CharSequence csq, int start, int end) {
        delegate.append(csq, start, end);
        return this;
    }

    public synchronized StringBuffer append(Object obj) {
        delegate.append(obj);
        return this;
    }

    public synchronized StringBuffer append(boolean b) {
        delegate.append(b);
        return this;
    }

    public synchronized StringBuffer append(int i) {
        delegate.append(i);
        return this;
    }

    public synchronized StringBuffer append(long l) {
        delegate.append(l);
        return this;
    }

    public synchronized StringBuffer append(float f) {
        delegate.append(f);
        return this;
    }

    public synchronized StringBuffer append(double d) {
        delegate.append(d);
        return this;
    }

    public synchronized StringBuffer append(char[] chars) {
        delegate.append(chars);
        return this;
    }

    public synchronized StringBuffer delete(int start, int end) {
        delegate.delete(start, end);
        return this;
    }

    public synchronized StringBuffer deleteCharAt(int index) {
        delegate.deleteCharAt(index);
        return this;
    }

    public synchronized StringBuffer insert(int offset, String str) {
        delegate.insert(offset, str);
        return this;
    }

    public synchronized StringBuffer replace(int start, int end, String str) {
        delegate.replace(start, end, str);
        return this;
    }

    public synchronized StringBuffer reverse() {
        delegate.reverse();
        return this;
    }

    public synchronized String substring(int start) {
        return delegate.substring(start);
    }

    public synchronized String substring(int start, int end) {
        return delegate.substring(start, end);
    }

    public synchronized CharSequence subSequence(int start, int end) {
        return delegate.subSequence(start, end);
    }

    public synchronized int indexOf(String str) {
        return delegate.indexOf(str);
    }

    public synchronized String toString() {
        return delegate.toString();
    }
}
