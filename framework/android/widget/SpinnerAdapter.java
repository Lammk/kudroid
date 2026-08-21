package android.widget;

import android.view.View;
import android.view.ViewGroup;

/**
 * android.widget.SpinnerAdapter — Adapter có view riêng cho phần "đang chọn"
 * hiển thị trên dropdown.
 */
public interface SpinnerAdapter extends Adapter {
    View getDropDownView(int position, View convertView, ViewGroup parent);
}
