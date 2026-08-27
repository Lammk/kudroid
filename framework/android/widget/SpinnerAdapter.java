package android.widget;

import android.view.View;
import android.view.ViewGroup;

/**
 * android.widget.SpinnerAdapter — Adapter has its own view for the "currently selected" part
 * displayed on dropdown.
 */
public interface SpinnerAdapter extends Adapter {
    View getDropDownView(int position, View convertView, ViewGroup parent);
}
