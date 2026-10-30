package android.text.util;

public class Linkify {
    public static final int WEB_URLS = 0x01;
    public static final int EMAIL_ADDRESSES = 0x02;
    public static final int PHONE_NUMBERS = 0x04;
    public static final int MAP_ADDRESSES = 0x08;
    public static final int ALL = WEB_URLS | EMAIL_ADDRESSES | PHONE_NUMBERS | MAP_ADDRESSES;

    public Linkify() {}

    public interface MatchFilter {
        boolean acceptMatch(CharSequence s, int start, int end);
    }

    public interface TransformFilter {
        String transformUrl(java.util.regex.Matcher match, String url);
    }

    /** Rejects matches that directly follow an '@' (the host part of an address). */
    public static final MatchFilter sUrlMatchFilter = new MatchFilter() {
        @Override
        public boolean acceptMatch(CharSequence s, int start, int end) {
            return start == 0 || s.charAt(start - 1) != '@';
        }
    };
}
