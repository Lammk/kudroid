package org.fmod;

public class MediaCodec {
    public MediaCodec() {}

    public boolean init(long handle) {
        return false;
    }

    public void release() {}

    public int getChannelCount() { return 2; }
    public int getSampleRate() { return 48000; }
    public long getLength() { return 0; }
    public int read(byte[] buffer, int size) { return 0; }
    public void seek(int position) {}
}
