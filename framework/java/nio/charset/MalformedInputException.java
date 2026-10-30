package java.nio.charset;

public class MalformedInputException extends CharacterCodingException {
    private final int inputLength;
    public MalformedInputException(int length) { inputLength = length; }
    public int getInputLength() { return inputLength; }
    @Override public String getMessage() { return "Input length = " + inputLength; }
}
