package java.security;

import java.util.Properties;

public abstract class Provider extends Properties {
    private static final long serialVersionUID = -4298000515446427739L;
    private final String name;
    private final double version;
    private final String info;

    protected Provider(String name, double version, String info) {
        this.name = name;
        this.version = version;
        this.info = info;
    }
    public String getName() { return name; }
    public double getVersion() { return version; }
    public String getInfo() { return info; }
    public String toString() { return name + " version " + version; }
}
