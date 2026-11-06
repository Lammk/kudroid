package android.speech.tts;

public class TextToSpeech {
    public static final int SUCCESS = 0;
    public static final int ERROR = -1;
    public static final int LANG_AVAILABLE = 0;
    public static final int LANG_COUNTRY_AVAILABLE = 1;
    public static final int LANG_COUNTRY_VAR_AVAILABLE = 2;
    public static final int LANG_MISSING_DATA = -1;
    public static final int LANG_NOT_SUPPORTED = -2;
    public TextToSpeech() {}

    public TextToSpeech(android.content.Context context, final OnInitListener listener) {
        if (context == null) throw new NullPointerException("context == null");
        if (listener != null) {
            new android.os.Handler(context.getMainLooper()).post(new Runnable() {
                public void run() { listener.onInit(ERROR); }
            });
        }
    }

    public void shutdown() {}

    public int setLanguage(java.util.Locale locale) { return LANG_NOT_SUPPORTED; }

    public int isLanguageAvailable(java.util.Locale locale) { return LANG_NOT_SUPPORTED; }

    public int stop() { return ERROR; }

    public interface OnInitListener {
        void onInit(int status);
    }
}
