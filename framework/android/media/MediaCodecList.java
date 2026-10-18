package android.media;

/** The codecs KuDroid's media layer accepts (see MediaShim); software decoders in effect. */
public final class MediaCodecList {
    public static final int REGULAR_CODECS = 0;
    public static final int ALL_CODECS = 1;

    private static final String[] VIDEO = {
        "video/avc", "video/hevc", "video/x-vnd.on2.vp8", "video/x-vnd.on2.vp9", "video/mp4v-es", "video/3gpp"
    };
    private static final String[] AUDIO = {
        "audio/mp4a-latm", "audio/mpeg", "audio/vorbis", "audio/opus", "audio/flac", "audio/raw", "audio/3gpp", "audio/amr-wb"
    };

    private static MediaCodecInfo[] sInfos;

    private final MediaCodecInfo[] mInfos;

    public MediaCodecList(int kind) {
        mInfos = infos();
    }

    private static synchronized MediaCodecInfo[] infos() {
        if (sInfos == null) {
            sInfos = new MediaCodecInfo[] {
                new MediaCodecInfo("kudroid.video.decoder", false, VIDEO),
                new MediaCodecInfo("kudroid.audio.decoder", false, AUDIO),
            };
        }
        return sInfos;
    }

    public MediaCodecInfo[] getCodecInfos() { return mInfos.clone(); }

    public static int getCodecCount() { return infos().length; }

    public static MediaCodecInfo getCodecInfoAt(int index) {
        MediaCodecInfo[] all = infos();
        if (index < 0 || index >= all.length) throw new IllegalArgumentException();
        return all[index];
    }

    public String findDecoderForFormat(MediaFormat format) {
        return find(format);
    }

    public String findEncoderForFormat(MediaFormat format) {
        return null;
    }

    private String find(MediaFormat format) {
        String mime = format != null ? format.getString(MediaFormat.KEY_MIME) : null;
        if (mime == null) return null;
        MediaCodecInfo[] all = infos();
        for (int i = 0; i < all.length; i++) {
            String[] types = all[i].getSupportedTypes();
            for (int j = 0; j < types.length; j++) {
                if (types[j].equalsIgnoreCase(mime)) return all[i].getName();
            }
        }
        return null;
    }
}
