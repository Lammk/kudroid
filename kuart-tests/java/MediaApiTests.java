import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;

/** The android.media classes a player reads constants from and queries for capabilities. */
public final class MediaApiTests {
    private static void check(boolean ok, String name) { TestSupport.check(ok, name); }

    public static String run() {
        TestSupport.report.setLength(0);
        try { constants(); } catch (Throwable t) { TestSupport.fail("constants", t); }
        try { capabilities(); } catch (Throwable t) { TestSupport.fail("capabilities", t); }
        return TestSupport.report.toString();
    }

    static void constants() {
        check(MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface == 0x7F000789, "COLOR_FormatSurface");
        check(MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible == 0x7F420888, "COLOR_FormatYUV420Flexible");
        check(MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Planar == 19, "COLOR_FormatYUV420Planar");
        check(MediaCodecInfo.CodecCapabilities.FEATURE_AdaptivePlayback.equals("adaptive-playback"), "FEATURE_AdaptivePlayback");
        check(MediaFormat.COLOR_RANGE_FULL == 1 && MediaFormat.COLOR_RANGE_LIMITED == 2, "MediaFormat color range");
        check(MediaFormat.COLOR_STANDARD_BT709 == 1 && MediaFormat.COLOR_STANDARD_BT2020 == 6, "MediaFormat color standard");
    }

    static void capabilities() {
        MediaCodecList list = new MediaCodecList(MediaCodecList.REGULAR_CODECS);
        MediaFormat vp8 = MediaFormat.createVideoFormat("video/x-vnd.on2.vp8", 1920, 1080);
        String name = list.findDecoderForFormat(vp8);
        check(name != null, "a VP8 decoder is listed");
        MediaFormat odd = MediaFormat.createVideoFormat("video/x-unknown", 16, 16);
        check(list.findDecoderForFormat(odd) == null, "an unknown format has none");

        MediaCodecInfo[] infos = list.getCodecInfos();
        check(infos.length > 0 && !infos[0].isEncoder(), "decoders are reported");
        MediaCodecInfo.CodecCapabilities caps = infos[0].getCapabilitiesForType("video/avc");
        check(caps.getMimeType().equals("video/avc"), "capabilities carry their mime");
        check(caps.colorFormats.length > 0, "and color formats");
        check(caps.getVideoCapabilities().isSizeSupported(1920, 1080), "1080p is supported");
        check(caps.getAudioCapabilities() == null, "a video type has no audio capabilities");
        check(infos[1].getCapabilitiesForType("audio/vorbis").getAudioCapabilities().getMaxInputChannelCount() >= 2,
                "audio capabilities report channels");
        boolean rejected = false;
        try { infos[0].getCapabilitiesForType("audio/vorbis"); } catch (IllegalArgumentException e) { rejected = true; }
        check(rejected, "an unsupported type is rejected");
    }
}
