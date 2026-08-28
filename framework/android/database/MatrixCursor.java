package android.database;

public class MatrixCursor extends AbstractCursor {
    private final String[] columnNames;
    private Object[] data;
    private int rowCount = 0;

    public MatrixCursor(String[] columnNames, int initialCapacity) {
        this.columnNames = columnNames;
        this.data = new Object[columnNames.length * initialCapacity];
    }
    public MatrixCursor(String[] columnNames) { this(columnNames, 16); }

    public class RowBuilder {
        private int index;
        private final int endIndex;
        RowBuilder(int row) {
            this.index = row * columnNames.length;
            this.endIndex = this.index + columnNames.length;
        }
        public RowBuilder add(Object columnValue) {
            if (index == endIndex) throw new RuntimeException("No more columns left.");
            data[index++] = columnValue;
            return this;
        }
        public RowBuilder add(String columnName, Object value) {
            return add(value);
        }
    }

    public RowBuilder newRow() {
        int row = rowCount++;
        ensureCapacity(rowCount * columnNames.length);
        return new RowBuilder(row);
    }
    public void addRow(Object[] columnValues) {
        if (columnValues.length != columnNames.length) throw new IllegalArgumentException("columnNames.length = " + columnNames.length + ", columnValues.length = " + columnValues.length);
        int start = rowCount++ * columnNames.length;
        ensureCapacity(rowCount * columnNames.length);
        System.arraycopy(columnValues, 0, data, start, columnNames.length);
    }
    private void ensureCapacity(int size) {
        if (size > data.length) {
            Object[] old = data;
            data = new Object[Math.max(size, data.length * 2)];
            System.arraycopy(old, 0, data, 0, old.length);
        }
    }
    public int getCount() { return rowCount; }
    public String[] getColumnNames() { return columnNames; }
    private Object get(int column) {
        if (column < 0 || column >= columnNames.length) throw new IllegalArgumentException("Requested column: " + column + ", # of columns: " + columnNames.length);
        if (mPos < 0) throw new RuntimeException("Before first row.");
        if (mPos >= rowCount) throw new RuntimeException("After last row.");
        return data[mPos * columnNames.length + column];
    }
    public String getString(int column) { Object v = get(column); return v != null ? v.toString() : null; }
    public short getShort(int column) { Object v = get(column); return (v instanceof Number) ? ((Number) v).shortValue() : 0; }
    public int getInt(int column) { Object v = get(column); return (v instanceof Number) ? ((Number) v).intValue() : 0; }
    public long getLong(int column) { Object v = get(column); return (v instanceof Number) ? ((Number) v).longValue() : 0L; }
    public float getFloat(int column) { Object v = get(column); return (v instanceof Number) ? ((Number) v).floatValue() : 0.0f; }
    public double getDouble(int column) { Object v = get(column); return (v instanceof Number) ? ((Number) v).doubleValue() : 0.0; }
    public boolean isNull(int column) { return get(column) == null; }
}
