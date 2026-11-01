package java.util.regex;

abstract class CharacterClassSet extends JointSet {
    abstract int accepts(int index, CharSequence input, MatchResultImpl result);

    public int matches(int index, CharSequence input, MatchResultImpl result) {
        int shift = accepts(index, input, result);
        return shift > 0 ? next.matches(index + shift, input, result) : -1;
    }
}
