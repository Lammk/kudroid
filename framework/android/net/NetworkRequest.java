package android.net;

import android.os.Parcel;
import android.os.Parcelable;

public class NetworkRequest implements Parcelable {
    public final NetworkCapabilities networkCapabilities;

    public NetworkRequest() {
        this.networkCapabilities = new NetworkCapabilities();
    }
    public NetworkRequest(NetworkCapabilities nc) {
        this.networkCapabilities = new NetworkCapabilities(nc);
    }

    public static class Builder {
        private final NetworkCapabilities capabilities = new NetworkCapabilities();

        public Builder() {
            capabilities.addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_RESTRICTED)
                    .addCapability(NetworkCapabilities.NET_CAPABILITY_TRUSTED)
                    .addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN);
        }
        public Builder addCapability(int capability) { capabilities.addCapability(capability); return this; }
        public Builder removeCapability(int capability) { capabilities.removeCapability(capability); return this; }
        public Builder addTransportType(int transportType) { capabilities.addTransportType(transportType); return this; }
        public Builder removeTransportType(int transportType) { capabilities.removeTransportType(transportType); return this; }
        public Builder clearCapabilities() { capabilities.clearAll(); return this; }
        public NetworkRequest build() {
            return new NetworkRequest(capabilities);
        }
    }

    public boolean canBeSatisfiedBy(NetworkCapabilities nc) { return networkCapabilities.satisfiedBy(nc); }
    public boolean hasCapability(int capability) { return networkCapabilities.hasCapability(capability); }
    public boolean hasTransport(int transportType) { return networkCapabilities.hasTransport(transportType); }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
