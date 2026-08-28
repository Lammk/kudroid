package android.net;

import android.os.Parcel;
import android.os.Parcelable;
import java.net.Socket;
import java.net.URL;
import java.net.URLConnection;
import java.io.IOException;

public class Network implements Parcelable {
    public Network() {}
    public Socket bindSocket(Socket socket) throws IOException { return socket; }
    public URLConnection openConnection(URL url) throws IOException { return url.openConnection(); }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
