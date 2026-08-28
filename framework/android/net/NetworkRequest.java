package android.net;

import android.os.Parcel;
import android.os.Parcelable;

public class NetworkRequest implements Parcelable {
    public final NetworkCapabilities networkCapabilities;

    public NetworkRequest() {
        this.networkCapabilities = new NetworkCapabilities();
    }
    public NetworkRequest(NetworkCapabilities nc) {
        this.networkCapabilities = nc;
    }

    public static class Builder {
        private final NetworkCapabilities capabilities = new NetworkCapabilities();

        public Builder() {}
        public Builder addCapability(int capability) { return this; }
        public Builder removeCapability(int capability) { return this; }
        public Builder addTransportType(int transportType) { return this; }
        public Builder removeTransportType(int transportType) { return this; }
        public NetworkRequest build() {
            return new NetworkRequest(capabilities);
        }
    }

    public boolean canBeSatisfiedBy(NetworkCapabilities nc) { return true; }
    public boolean hasCapability(int capability) { return true; }
    public boolean hasTransport(int transportType) { return true; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
