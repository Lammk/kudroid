package android.text;

public class Html {
    public static final int FROM_HTML_MODE_LEGACY = 0;
    public static final int FROM_HTML_MODE_COMPACT = 63;

    public interface ImageGetter {
        android.graphics.drawable.Drawable getDrawable(String source);
    }
    public interface TagHandler {
        void handleTag(boolean opening, String tag, Editable output, org.xml.sax.XMLReader xmlReader);
    }
    public static Spanned fromHtml(String source) {
        return new SpannableString(source != null ? source.replaceAll("<[^>]*>", "") : "");
    }
    public static Spanned fromHtml(String source, int flags) {
        return fromHtml(source);
    }
    public static String toHtml(Spanned text) {
        return text != null ? text.toString() : "";
    }
}
