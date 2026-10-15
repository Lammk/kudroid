package java.util.logging;

import java.io.IOException;

public class SocketHandler extends StreamHandler {
    public SocketHandler() throws IOException {}
    public SocketHandler(String host, int port) throws IOException {}
}
