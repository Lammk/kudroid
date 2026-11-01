import dalvik.system.InMemoryDexClassLoader;
import java.io.*;
import java.nio.*;
import java.util.*;

public class DynamicLoadingTests {
    private static int checks;
    private static void check(boolean value, String message) {
        checks++;
        if (!value) throw new AssertionError(message);
    }
    private static int value(ClassLoader loader, String name) throws Exception {
        return ((Integer) loader.loadClass(name).getMethod("value").invoke(null)).intValue();
    }
    private static void missing(ClassLoader loader, String name, boolean suppressed) throws Exception {
        try { loader.loadClass(name); check(false, "unexpectedly loaded " + name); }
        catch (ClassNotFoundException expected) {
            check(!suppressed || (expected.getSuppressed().length == 1 &&
                    expected.getSuppressed()[0] instanceof IOException), "DEX failure has I/O context");
        }
    }
    private static byte[] padded(byte[] bytes, int prefix, int suffix) {
        byte[] result = new byte[prefix + bytes.length + suffix];
        Arrays.fill(result, (byte) 0x7f);
        System.arraycopy(bytes, 0, result, prefix, bytes.length);
        return result;
    }
    private static void loaders(byte[][] dex) throws Exception {
        ClassLoader parent = ClassLoader.getSystemClassLoader();
        missing(parent, "DynamicPayloadHeap", false);
        byte[] heapBytes = padded(dex[0], 13, 17);
        ByteBuffer heap = ByteBuffer.wrap(heapBytes, 13, dex[0].length);
        heap.mark();
        InMemoryDexClassLoader first = new InMemoryDexClassLoader(heap, parent);
        check(first.getParent() == parent && first instanceof dalvik.system.BaseDexClassLoader,
                "loader parent and Android superclass");
        check(heap.position() == 13 && heap.limit() == 13 + dex[0].length, "heap bounds unchanged");
        heap.reset();
        check(heap.position() == 13, "heap mark preserved");
        Arrays.fill(heapBytes, (byte) 0);
        check(value(first, "DynamicPayloadHeap") == 42, "heap DEX owned after input mutation");
        check(first.loadClass("java.lang.String") == String.class, "framework delegation preserved");

        ByteBuffer direct = ByteBuffer.allocateDirect(dex[1].length + 12);
        direct.position(5);
        direct.put(dex[1]);
        direct.limit(direct.position());
        direct.position(5);
        direct.mark();
        InMemoryDexClassLoader second = new InMemoryDexClassLoader(new ByteBuffer[]{direct}, parent);
        check(direct.position() == 5 && direct.limit() == 5 + dex[1].length, "direct bounds unchanged");
        direct.reset();
        check(direct.position() == 5, "direct mark preserved");
        direct.put(5, (byte) 0);
        check(value(second, "DynamicPayloadDirect") == 43, "direct DEX owned after input mutation");

        byte[] viewBytes = padded(dex[2], 19, 11);
        ByteBuffer view = TestBufferViews.readOnlySlice(viewBytes, 17, dex[2].length + 2);
        view.position(2);
        view.mark();
        InMemoryDexClassLoader third = new InMemoryDexClassLoader(new ByteBuffer[]{view}, "/data/app/lib", parent);
        check(view.isReadOnly() && !view.hasArray(), "read-only view hides backing array");
        check(view.position() == 2 && view.limit() == dex[2].length + 2, "slice bounds unchanged");
        view.reset();
        check(view.position() == 2, "slice mark preserved");
        Arrays.fill(viewBytes, (byte) 0);
        check(value(third, "DynamicPayloadView") == 44, "read-only sliced DEX executes");
        check(third.getLdLibraryPath().equals("/data/app/lib"), "library search path retained");

        InMemoryDexClassLoader multi = new InMemoryDexClassLoader(new ByteBuffer[]{
                ByteBuffer.wrap(dex[4]), ByteBuffer.wrap(dex[3])}, parent);
        check(value(multi, "DynamicPayloadCaller") == 46, "cross-buffer static dependency resolves");

        InMemoryDexClassLoader invalid = new InMemoryDexClassLoader(new ByteBuffer[]{
                ByteBuffer.wrap(dex[5]), ByteBuffer.wrap(new byte[]{1, 2, 3})}, parent);
        missing(invalid, "DynamicPayloadRollback", true);
        missing(parent, "DynamicPayloadRollback", false);
        byte[] corrupt = Arrays.copyOf(dex[5], dex[5].length);
        corrupt[12] ^= 1;
        missing(new InMemoryDexClassLoader(ByteBuffer.wrap(corrupt), parent), "DynamicPayloadRollback", true);
        missing(new InMemoryDexClassLoader(ByteBuffer.allocate(0), parent), "DynamicPayloadRollback", true);
        missing(new InMemoryDexClassLoader(new ByteBuffer[0], parent), "DynamicPayloadRollback", true);
        try { new InMemoryDexClassLoader((ByteBuffer) null, parent); check(false, "null single buffer"); }
        catch (NullPointerException expected) { check(true, "null single buffer rejected"); }
        try { new InMemoryDexClassLoader((ByteBuffer[]) null, parent); check(false, "null buffer array"); }
        catch (NullPointerException expected) { check(true, "null buffer array rejected"); }
        try { new InMemoryDexClassLoader(new ByteBuffer[]{ByteBuffer.wrap(dex[5]), null}, parent);
              check(false, "null element"); }
        catch (NullPointerException expected) { check(true, "null element rejected before registration"); }
        missing(parent, "DynamicPayloadRollback", false);
        check(value(new InMemoryDexClassLoader(ByteBuffer.wrap(dex[5]), null),
                "DynamicPayloadRollback") == 47, "valid load succeeds after rejected transactions");
    }
    private static void temporaryFiles() throws Exception {
        File directory = new File("/data/local/tmp/dynamic-tests-" + System.nanoTime());
        check(directory.mkdir(), "temporary test directory created");
        try {
            File one = File.createTempFile("dex", null, directory);
            File two = File.createTempFile("dex", null, directory);
            check(one.exists() && one.isFile() && one.length() == 0, "temporary file already exists and is empty");
            check(!one.equals(two), "temporary names differ");
            check(one.getName().startsWith("dex") && one.getName().endsWith(".tmp"), "default suffix and prefix");
            check(one.getParentFile().equals(directory), "guest directory retained");
            check(!one.createNewFile(), "exclusive creation preserves an existing file");
            FileOutputStream output = new FileOutputStream(one);
            output.write(new byte[]{1, 2, 3});
            output.close();
            check(one.length() == 3 && !one.createNewFile() && one.length() == 3, "collision never truncates data");
            File custom = File.createTempFile("hello", ".dex", directory);
            check(custom.getName().startsWith("hello") && custom.getName().endsWith(".dex"), "custom suffix retained");
            File emptySuffix = File.createTempFile("dex", "", directory);
            check(!emptySuffix.getName().endsWith(".tmp"), "empty suffix retained");
            String old = System.setProperty("java.io.tmpdir", directory.getPath());
            try {
                File def = File.createTempFile("default", null);
                check(def.getParentFile().equals(directory), "current default temp property honored");
                check(def.delete(), "default temporary file deletes");
            } finally { System.setProperty("java.io.tmpdir", old); }
            try { File.createTempFile(null, null, directory); check(false, "null prefix accepted"); }
            catch (NullPointerException expected) { check(true, "null prefix rejected"); }
            try { File.createTempFile("ab", null, directory); check(false, "short prefix accepted"); }
            catch (IllegalArgumentException expected) { check(true, "short prefix rejected"); }
            for (String prefix : new String[]{"../bad", "a/b", "a\u0000b"}) {
                try { File.createTempFile(prefix, null, directory); check(false, "invalid prefix accepted"); }
                catch (IOException expected) { check(true, "invalid prefix rejected"); }
            }
            try { File.createTempFile("dex", "/bad", directory); check(false, "invalid suffix accepted"); }
            catch (IOException expected) { check(true, "invalid suffix rejected"); }
            try { File.createTempFile("dex", "x\u0000y", directory); check(false, "NUL suffix accepted"); }
            catch (IOException expected) { check(true, "NUL suffix rejected"); }
            try { new File(directory, "bad\u0000tail").createNewFile(); check(false, "NUL path accepted"); }
            catch (IOException expected) { check(true, "NUL path rejected"); }
            try { new File("").createNewFile(); check(false, "empty path accepted"); }
            catch (IOException expected) { check(true, "empty path rejected"); }
            try { File.createTempFile("dex", null, new File(directory, "absent")); check(false, "missing directory accepted"); }
            catch (IOException expected) { check(true, "missing directory rejected"); }
            try { File.createTempFile("dex", null, one); check(false, "file used as directory"); }
            catch (IOException expected) { check(true, "file directory rejected"); }
            try { new File(directory, "absent/file").createNewFile(); check(false, "I/O error hidden"); }
            catch (IOException expected) { check(true, "exclusive create exposes real I/O error"); }
            final Set<String> names = new HashSet<String>();
            final Throwable[] failure = new Throwable[1];
            final File dir = directory;
            Thread[] threads = new Thread[4];
            for (int i = 0; i < threads.length; i++) {
                threads[i] = new Thread(new Runnable() {
                    public void run() {
                        try {
                            for (int j = 0; j < 15; j++) {
                                File file = File.createTempFile("parallel", null, dir);
                                synchronized (names) {
                                    if (!names.add(file.getName()) || !file.isFile() || file.length() != 0)
                                        throw new AssertionError("invalid concurrent temp file");
                                }
                            }
                        } catch (Throwable error) { synchronized (names) { failure[0] = error; } }
                    }
                });
                threads[i].start();
            }
            for (Thread thread : threads) thread.join();
            if (failure[0] != null) throw new AssertionError(failure[0]);
            check(names.size() == 60, "concurrent creators produce distinct real files");
        } finally {
            File[] files = directory.listFiles();
            if (files != null) for (File file : files) file.delete();
            check(directory.delete(), "temporary test directory cleaned");
        }
    }
    public static int run(byte[][] dex) throws Exception {
        checks = 0;
        loaders(dex);
        temporaryFiles();
        return checks;
    }
}
