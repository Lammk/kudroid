package android.widget;

import android.view.View;
import android.view.ViewGroup;

/**
 * android.widget.ListAdapter — Adapter dùng cho ListView (thêm khái niệm
 * "item có thể chọn").
 */
public interface ListAdapter extends Adapter {
    boolean areAllItemsEnabled();

    boolean isEnabled(int position);
}
