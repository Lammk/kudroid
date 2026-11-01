public class DynamicPayloadCaller {
    public static int value() { return DynamicPayloadDependency.value() + 1; }
}
