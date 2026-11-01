import android.content.Context;
import android.content.ContextWrapper;
import android.os.StrictMode;
import android.os.storage.StorageManager;
import android.speech.tts.TextToSpeech;
import java.io.File;
import java.io.FileNotFoundException;
import java.net.*;
import java.util.*;

public class StartupFrameworkTests {
    private static int checks;
    private static void check(boolean value, String message) {
        checks++;
        if (!value) throw new AssertionError(message);
    }
    public static int run() throws Exception {
        checks = 0;
        StrictMode.VmPolicy original = StrictMode.getVmPolicy();
        check(original != null, "VM policy starts non-null");
        StrictMode.VmPolicy replacement = new StrictMode.VmPolicy();
        StrictMode.setVmPolicy(replacement);
        check(StrictMode.getVmPolicy() == replacement, "VM policy preserves identity");
        try { StrictMode.setVmPolicy(null); check(false, "null policy accepted"); }
        catch (NullPointerException expected) { check(true, "null policy rejected"); }
        check(StrictMode.getVmPolicy() == replacement, "invalid policy leaves state intact");
        StrictMode.setVmPolicy(original);

        Object[] items = {"a", "b", "c", "d"};
        Arrays.fill(items, 1, 3, "x");
        check(items[0].equals("a") && items[1].equals("x") && items[2].equals("x")
                && items[3].equals("d"), "fill mutates only the requested range");
        Arrays.fill(items, 4, 4, null);
        check(items[3].equals("d"), "empty fill at end leaves contents intact");
        try { Arrays.fill(items, 3, 2, null); check(false, "reversed range accepted"); }
        catch (IllegalArgumentException expected) { check(true, "reversed range rejected"); }
        try { Arrays.fill(items, -1, 2, null); check(false, "negative range accepted"); }
        catch (ArrayIndexOutOfBoundsException expected) { check(true, "negative range rejected"); }
        try { Arrays.fill(items, 1, 5, null); check(false, "oversized range accepted"); }
        catch (ArrayIndexOutOfBoundsException expected) { check(true, "oversized range rejected"); }
        try { Arrays.fill((Object[]) null, 2, 1, null); check(false, "null array accepted"); }
        catch (NullPointerException expected) { check(true, "null array rejected first"); }
        String[] strings = {"a"};
        try { Arrays.fill(strings, 0, 1, new Object()); check(false, "incompatible value accepted"); }
        catch (ArrayStoreException expected) { check(true, "array component type enforced"); }

        Context context = new ContextWrapper(null);
        StorageManager storage = (StorageManager) context.getSystemService(Context.STORAGE_SERVICE);
        check(storage != null, "storage service reachable by name");
        check(context.getSystemService(StorageManager.class) instanceof StorageManager, "storage service reachable by class");
        check(Context.STORAGE_SERVICE.equals(context.getSystemServiceName(StorageManager.class)), "storage service name resolved");
        String[] paths = {"/data", "/data/data/example/files", "/sdcard", "/storage/emulated/0/a",
                "/storage/self/primary/a", "/mnt/sdcard/a", "/data/one/../two"};
        for (String path : paths) check(StorageManager.UUID_DEFAULT.equals(storage.getUuidForPath(new File(path))),
                                       "supported volume UUID");
        for (String path : new String[]{"/data-other", "/unknown", "/data/../unknown"}) {
            try { storage.getUuidForPath(new File(path)); check(false, "unknown volume accepted"); }
            catch (FileNotFoundException expected) { check(true, "unknown volume rejected"); }
        }
        try { storage.getUuidForPath(null); check(false, "null path accepted"); }
        catch (NullPointerException expected) { check(true, "null path rejected"); }

        Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
        int addresses = 0;
        boolean loopback = false;
        while (interfaces.hasMoreElements()) {
            NetworkInterface net = interfaces.nextElement();
            check(NetworkInterface.getByName(net.getName()) != null, "enumerated interface resolves by name");
            List<InterfaceAddress> bindings = net.getInterfaceAddresses();
            check(bindings != null, "interface address list is non-null");
            Enumeration<InetAddress> ips = net.getInetAddresses();
            for (InterfaceAddress binding : bindings) {
                addresses++;
                check(ips.hasMoreElements() && ips.nextElement().equals(binding.getAddress()), "address APIs agree");
                int bits = binding.getAddress().getAddress().length * 8;
                check(binding.getNetworkPrefixLength() >= 0 && binding.getNetworkPrefixLength() <= bits, "valid network prefix");
                check(bits == 32 || binding.getBroadcast() == null, "IPv6 has no broadcast address");
                check(NetworkInterface.getByInetAddress(binding.getAddress()) != null, "interface resolves by address");
                check(binding.equals(binding) && !binding.equals(null), "interface address equality");
                if (binding.getAddress().isLoopbackAddress()) loopback = true;
            }
            check(!ips.hasMoreElements(), "address enumeration has no extra values");
        }
        check(addresses > 0 && loopback, "host interface snapshot includes real loopback");
        check(NetworkInterface.getByName("__missing_interface__") == null, "unknown interface returns null");
        try { NetworkInterface.getByName(null); check(false, "null interface name accepted"); }
        catch (NullPointerException expected) { check(true, "null interface name rejected"); }

        android.os.HandlerThread thread = new android.os.HandlerThread("SpeechInit");
        thread.start();
        final android.os.Looper looper = thread.getLooper();
        final Object lock = new Object();
        final int[] status = {123};
        Context speechContext = new ContextWrapper(null) {
            public android.os.Looper getMainLooper() { return looper; }
        };
        TextToSpeech speech = new TextToSpeech(speechContext, new TextToSpeech.OnInitListener() {
            public void onInit(int value) { synchronized (lock) { status[0] = value; lock.notifyAll(); } }
        });
        try {
            long end = System.currentTimeMillis() + 3000;
            synchronized (lock) {
                while (status[0] == 123 && System.currentTimeMillis() < end)
                    lock.wait(Math.max(1, end - System.currentTimeMillis()));
            }
            check(status[0] == TextToSpeech.ERROR, "speech init reports unavailable backend");
        } finally { speech.shutdown(); thread.quit(); thread.join(3000); }
        return checks;
    }
}
