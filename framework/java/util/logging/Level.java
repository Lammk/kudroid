package java.util.logging;

import java.io.Serializable;

public class Level implements Serializable {
    private static final long serialVersionUID = -8176051834860883308L;

    private final String name;
    private final int value;

    public static final Level OFF = new Level("OFF", Integer.MAX_VALUE);
    public static final Level SEVERE = new Level("SEVERE", 1000);
    public static final Level WARNING = new Level("WARNING", 900);
    public static final Level INFO = new Level("INFO", 800);
    public static final Level CONFIG = new Level("CONFIG", 700);
    public static final Level FINE = new Level("FINE", 500);
    public static final Level FINER = new Level("FINER", 400);
    public static final Level FINEST = new Level("FINEST", 300);
    public static final Level ALL = new Level("ALL", Integer.MIN_VALUE);

    protected Level(String name, int value) {
        if (name == null) throw new NullPointerException();
        this.name = name;
        this.value = value;
    }

    public String getName() { return name; }
    public int intValue() { return value; }
    public final String toString() { return name; }

    public static synchronized Level parse(String name) {
        if (name == null) throw new NullPointerException();
        if (name.equalsIgnoreCase("OFF")) return OFF;
        if (name.equalsIgnoreCase("SEVERE")) return SEVERE;
        if (name.equalsIgnoreCase("WARNING")) return WARNING;
        if (name.equalsIgnoreCase("INFO")) return INFO;
        if (name.equalsIgnoreCase("CONFIG")) return CONFIG;
        if (name.equalsIgnoreCase("FINE")) return FINE;
        if (name.equalsIgnoreCase("FINER")) return FINER;
        if (name.equalsIgnoreCase("FINEST")) return FINEST;
        if (name.equalsIgnoreCase("ALL")) return ALL;
        try {
            return new Level(name, Integer.parseInt(name));
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException("Unknown level: " + name);
        }
    }

    public boolean equals(Object ox) {
        if (!(ox instanceof Level)) return false;
        Level lx = (Level) ox;
        return lx.value == this.value;
    }

    public int hashCode() {
        return this.value;
    }
}
