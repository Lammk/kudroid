package java.util;

import java.io.ByteArrayOutputStream;
import java.io.Closeable;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.util.regex.MatchResult;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Text scanner parsing primitive types and strings using regular expressions.
 */
public final class Scanner implements Iterator<String>, Closeable {
    private static final Pattern DEFAULT_DELIMITER = Pattern.compile("\\s+");

    private final String mBuffer;
    private int mPosition = 0;
    private Pattern mDelimiter = DEFAULT_DELIMITER;
    private MatchResult mLastMatch = null;
    private boolean mClosed = false;
    private int mRadix = 10;
    private Locale mLocale = Locale.getDefault();
    private IOException mIoException = null;

    public Scanner(String source) {
        if (source == null) throw new NullPointerException("source == null");
        mBuffer = source;
    }

    public Scanner(InputStream source) {
        this(source, "UTF-8");
    }

    public Scanner(InputStream source, String charsetName) {
        if (source == null) throw new NullPointerException("source == null");
        String text = "";
        try {
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = source.read(buf, 0, buf.length)) > 0) {
                baos.write(buf, 0, n);
            }
            text = baos.toString();
        } catch (IOException e) {
            mIoException = e;
        }
        mBuffer = text;
    }

    public Scanner(Reader source) {
        if (source == null) throw new NullPointerException("source == null");
        String text = "";
        try {
            StringBuilder sb = new StringBuilder();
            char[] buf = new char[4096];
            int n;
            while ((n = source.read(buf, 0, buf.length)) > 0) {
                sb.append(buf, 0, n);
            }
            text = sb.toString();
        } catch (IOException e) {
            mIoException = e;
        }
        mBuffer = text;
    }

    public Scanner(Readable source) {
        if (source == null) throw new NullPointerException("source == null");
        if (source instanceof Reader) {
            String text = "";
            try {
                StringBuilder sb = new StringBuilder();
                char[] buf = new char[4096];
                Reader r = (Reader) source;
                int n;
                while ((n = r.read(buf, 0, buf.length)) > 0) {
                    sb.append(buf, 0, n);
                }
                text = sb.toString();
            } catch (IOException e) {
                mIoException = e;
            }
            mBuffer = text;
        } else {
            mBuffer = source.toString();
        }
    }

    public Scanner(File source) throws FileNotFoundException {
        this(new FileInputStream(source));
    }

    public Scanner(File source, String charsetName) throws FileNotFoundException {
        this(new FileInputStream(source), charsetName);
    }

    public String findWithinHorizon(Pattern pattern, int horizon) {
        checkClosed();
        if (pattern == null) throw new NullPointerException("pattern == null");
        if (horizon < 0) throw new IllegalArgumentException("horizon < 0");

        int horizonEnd = (horizon == 0) ? mBuffer.length() : Math.min(mBuffer.length(), mPosition + horizon);
        Matcher matcher = pattern.matcher(mBuffer);
        matcher.region(mPosition, horizonEnd);
        if (matcher.find()) {
            mPosition = matcher.end();
            mLastMatch = matcher.toMatchResult();
            return matcher.group();
        }
        return null;
    }

    public String findWithinHorizon(String pattern, int horizon) {
        return findWithinHorizon(Pattern.compile(pattern), horizon);
    }

    public String findInLine(Pattern pattern) {
        checkClosed();
        if (pattern == null) throw new NullPointerException("pattern == null");
        int lineEnd = mBuffer.length();
        for (int i = mPosition; i < mBuffer.length(); i++) {
            char c = mBuffer.charAt(i);
            if (c == '\n' || c == '\r') {
                lineEnd = i;
                break;
            }
        }
        Matcher matcher = pattern.matcher(mBuffer);
        matcher.region(mPosition, lineEnd);
        if (matcher.find()) {
            mPosition = matcher.end();
            mLastMatch = matcher.toMatchResult();
            return matcher.group();
        }
        return null;
    }

    public String findInLine(String pattern) {
        return findInLine(Pattern.compile(pattern));
    }

    private void skipLeadingDelimiters() {
        Matcher matcher = mDelimiter.matcher(mBuffer);
        matcher.region(mPosition, mBuffer.length());
        while (mPosition < mBuffer.length() && matcher.lookingAt()) {
            mPosition = matcher.end();
            matcher.region(mPosition, mBuffer.length());
        }
    }

    public boolean hasNext() {
        checkClosed();
        skipLeadingDelimiters();
        return mPosition < mBuffer.length();
    }

    public String next() {
        checkClosed();
        if (!hasNext()) throw new NoSuchElementException();
        Matcher matcher = mDelimiter.matcher(mBuffer);
        matcher.region(mPosition, mBuffer.length());
        int tokenStart = mPosition;
        int tokenEnd;
        if (matcher.find()) {
            tokenEnd = matcher.start();
            mPosition = matcher.end();
        } else {
            tokenEnd = mBuffer.length();
            mPosition = mBuffer.length();
        }
        return mBuffer.substring(tokenStart, tokenEnd);
    }

    public boolean hasNext(Pattern pattern) {
        checkClosed();
        skipLeadingDelimiters();
        if (mPosition >= mBuffer.length()) return false;
        Matcher delimMatcher = mDelimiter.matcher(mBuffer);
        delimMatcher.region(mPosition, mBuffer.length());
        int end = delimMatcher.find() ? delimMatcher.start() : mBuffer.length();
        Matcher patMatcher = pattern.matcher(mBuffer.substring(mPosition, end));
        return patMatcher.matches();
    }

    public boolean hasNext(String pattern) {
        return hasNext(Pattern.compile(pattern));
    }

    public String next(Pattern pattern) {
        if (!hasNext(pattern)) throw new NoSuchElementException();
        return next();
    }

    public String next(String pattern) {
        return next(Pattern.compile(pattern));
    }

    public boolean hasNextLine() {
        checkClosed();
        return mPosition < mBuffer.length();
    }

    public String nextLine() {
        checkClosed();
        if (!hasNextLine()) throw new NoSuchElementException();
        int start = mPosition;
        int lineEnd = mBuffer.length();
        int nextPos = mBuffer.length();
        for (int i = start; i < mBuffer.length(); i++) {
            char c = mBuffer.charAt(i);
            if (c == '\n') {
                lineEnd = i;
                nextPos = i + 1;
                break;
            } else if (c == '\r') {
                lineEnd = i;
                if (i + 1 < mBuffer.length() && mBuffer.charAt(i + 1) == '\n') {
                    nextPos = i + 2;
                } else {
                    nextPos = i + 1;
                }
                break;
            }
        }
        mPosition = nextPos;
        return mBuffer.substring(start, lineEnd);
    }

    public boolean hasNextInt() { return hasNextInt(mRadix); }
    public boolean hasNextInt(int radix) {
        checkClosed();
        int saved = mPosition;
        try {
            if (!hasNext()) return false;
            Integer.parseInt(next(), radix);
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            mPosition = saved;
        }
    }

    public int nextInt() { return nextInt(mRadix); }
    public int nextInt(int radix) {
        return Integer.parseInt(next(), radix);
    }

    public boolean hasNextLong() { return hasNextLong(mRadix); }
    public boolean hasNextLong(int radix) {
        checkClosed();
        int saved = mPosition;
        try {
            if (!hasNext()) return false;
            Long.parseLong(next(), radix);
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            mPosition = saved;
        }
    }

    public long nextLong() { return nextLong(mRadix); }
    public long nextLong(int radix) {
        return Long.parseLong(next(), radix);
    }

    public boolean hasNextFloat() {
        checkClosed();
        int saved = mPosition;
        try {
            if (!hasNext()) return false;
            Float.parseFloat(next());
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            mPosition = saved;
        }
    }

    public float nextFloat() {
        return Float.parseFloat(next());
    }

    public boolean hasNextDouble() {
        checkClosed();
        int saved = mPosition;
        try {
            if (!hasNext()) return false;
            Double.parseDouble(next());
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            mPosition = saved;
        }
    }

    public double nextDouble() {
        return Double.parseDouble(next());
    }

    public boolean hasNextBoolean() {
        checkClosed();
        int saved = mPosition;
        try {
            if (!hasNext()) return false;
            String s = next();
            return "true".equalsIgnoreCase(s) || "false".equalsIgnoreCase(s);
        } finally {
            mPosition = saved;
        }
    }

    public boolean nextBoolean() {
        String s = next();
        if ("true".equalsIgnoreCase(s)) return true;
        if ("false".equalsIgnoreCase(s)) return false;
        throw new InputMismatchException();
    }

    public boolean hasNextByte() { return hasNextByte(mRadix); }
    public boolean hasNextByte(int radix) {
        checkClosed();
        int saved = mPosition;
        try {
            if (!hasNext()) return false;
            Byte.parseByte(next(), radix);
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            mPosition = saved;
        }
    }

    public byte nextByte() { return nextByte(mRadix); }
    public byte nextByte(int radix) {
        return Byte.parseByte(next(), radix);
    }

    public boolean hasNextShort() { return hasNextShort(mRadix); }
    public boolean hasNextShort(int radix) {
        checkClosed();
        int saved = mPosition;
        try {
            if (!hasNext()) return false;
            Short.parseShort(next(), radix);
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            mPosition = saved;
        }
    }

    public short nextShort() { return nextShort(mRadix); }
    public short nextShort(int radix) {
        return Short.parseShort(next(), radix);
    }

    public MatchResult match() {
        if (mLastMatch == null) throw new IllegalStateException();
        return mLastMatch;
    }

    public Scanner useDelimiter(Pattern pattern) {
        if (pattern == null) throw new NullPointerException("pattern == null");
        mDelimiter = pattern;
        return this;
    }

    public Scanner useDelimiter(String pattern) {
        return useDelimiter(Pattern.compile(pattern));
    }

    public Pattern delimiter() {
        return mDelimiter;
    }

    public Scanner skip(Pattern pattern) {
        checkClosed();
        if (pattern == null) throw new NullPointerException("pattern == null");
        Matcher matcher = pattern.matcher(mBuffer);
        matcher.region(mPosition, mBuffer.length());
        if (matcher.lookingAt()) {
            mPosition = matcher.end();
            return this;
        }
        throw new NoSuchElementException();
    }

    public Scanner skip(String pattern) {
        return skip(Pattern.compile(pattern));
    }

    public Scanner reset() {
        mDelimiter = DEFAULT_DELIMITER;
        mRadix = 10;
        mLocale = Locale.getDefault();
        return this;
    }

    public int radix() { return mRadix; }
    public Scanner useRadix(int radix) {
        if (radix < Character.MIN_RADIX || radix > Character.MAX_RADIX) {
            throw new IllegalArgumentException("radix: " + radix);
        }
        mRadix = radix;
        return this;
    }

    public Locale locale() { return mLocale; }
    public Scanner useLocale(Locale locale) {
        mLocale = locale != null ? locale : Locale.getDefault();
        return this;
    }

    public IOException ioException() { return mIoException; }

    public void remove() { throw new UnsupportedOperationException(); }

    public void close() {
        mClosed = true;
    }

    private void checkClosed() {
        if (mClosed) throw new IllegalStateException("Scanner closed");
    }
}
