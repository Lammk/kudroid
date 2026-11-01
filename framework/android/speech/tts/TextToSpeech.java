package android.speech.tts;

public class TextToSpeech {
    public static final int SUCCESS = 0;
    public static final int ERROR = -1;
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

    public interface OnInitListener {
        void onInit(int status);
    }
}
