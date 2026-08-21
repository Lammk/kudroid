package android.database;

/**
 * android.database.DataSetObserver — nhận thông báo khi dữ liệu của Adapter đổi.
 *
 * Android khai báo abstract class với hai hàm rỗng; lớp con override cái cần.
 */
public abstract class DataSetObserver {
    public void onChanged() {
    }

    public void onInvalidated() {
    }
}
