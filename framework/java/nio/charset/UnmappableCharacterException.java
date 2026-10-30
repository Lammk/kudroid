package java.nio.charset;

public class UnmappableCharacterException extends CharacterCodingException {
    private final int inputLength;
    public UnmappableCharacterException(int length) { inputLength = length; }
    public int getInputLength() { return inputLength; }
    @Override public String getMessage() { return "Input length = " + inputLength; }
}
