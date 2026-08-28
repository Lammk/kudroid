package java.net;

public class URISyntaxException extends Exception {
    private static final long serialVersionUID = 2137022898109355620L;
    private final String input;
    private final int index;

    public URISyntaxException(String input, String reason, int index) {
        super(reason);
        this.input = input;
        this.index = index;
    }
    public URISyntaxException(String input, String reason) {
        this(input, reason, -1);
    }
    public String getInput() { return input; }
    public String getReason() { return getMessage(); }
    public int getIndex() { return index; }
}
