package java.util.regex;

class CharacterClassQuantifierSet extends GroupQuantifierSet {
    private final CharacterClassSet characters;

    CharacterClassQuantifierSet(CharacterClassSet characters, AbstractSet next, int type) {
        super(characters, next, type);
        this.characters = characters;
    }

    public int matches(int index, CharSequence input, MatchResultImpl result) {
        int remaining = Math.max(0, result.getRightBound() - index);
        int[] positions = new int[Math.min(16, remaining)];
        int count = 0;
        int shift;
        // Record UTF-16 boundaries without recursing once per character.
        while ((shift = characters.accepts(index, input, result)) > 0) {
            if (count == positions.length) {
                int length = positions.length + Math.min(positions.length, remaining - positions.length);
                int[] grown = new int[length];
                System.arraycopy(positions, 0, grown, 0, count);
                positions = grown;
            }
            positions[count++] = index;
            index += shift;
        }
        while (true) {
            int matched = next.matches(index, input, result);
            if (matched >= 0) return matched;
            if (count == 0) return -1;
            index = positions[--count];
        }
    }
}
