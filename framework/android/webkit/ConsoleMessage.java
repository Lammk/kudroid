package android.webkit;

public class ConsoleMessage {
    public enum MessageLevel { TIP, LOG, WARNING, ERROR, DEBUG }
    private final String mMessage;
    private final String mSourceId;
    private final int mLineNumber;
    private final MessageLevel mLevel;

    public ConsoleMessage(String message, String sourceId, int lineNumber, MessageLevel msgLevel) {
        mMessage = message;
        mSourceId = sourceId;
        mLineNumber = lineNumber;
        mLevel = msgLevel;
    }
    public String message() { return mMessage; }
    public String sourceId() { return mSourceId; }
    public int lineNumber() { return mLineNumber; }
    public MessageLevel messageLevel() { return mLevel; }
}
