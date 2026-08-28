package java.util;

/**
 * java.util.ListResourceBundle — bundle backed by a getContents() table.
 *
 * Harmony's regex declares its predefined character classes (\p{Alpha}, \p{Digit},
 * \p{InGreek}, ...) as a ListResourceBundle subclass, so this is on the critical path
 * for compiling a pattern that uses any of them.
 */
public abstract class ListResourceBundle extends ResourceBundle {
    private HashMap<String, Object> table;

    public ListResourceBundle() {
    }

    protected abstract Object[][] getContents();

    private void initTable() {
        if (table != null) return;
        table = new HashMap<String, Object>();
        Object[][] contents = getContents();
        if (contents == null) return;
        for (int i = 0; i < contents.length; i++) {
            Object[] row = contents[i];
            if (row == null || row.length < 2 || row[0] == null) continue;
            table.put((String) row[0], row[1]);
        }
    }

    @Override
    protected Object handleGetObject(String key) {
        initTable();
        return key == null ? null : table.get(key);
    }

    @Override
    public Enumeration<String> getKeys() {
        initTable();
        final Iterator<String> it = table.keySet().iterator();
        return new Enumeration<String>() {
            @Override
            public boolean hasMoreElements() {
                return it.hasNext();
            }

            @Override
            public String nextElement() {
                return it.next();
            }
        };
    }
}
