package android.widget;

import android.view.View;
import android.view.ViewGroup;

/**
 * android.widget.ListAdapter — Adapter for ListView (added concept
 * "selectable item").
 */
public interface ListAdapter extends Adapter {
    boolean areAllItemsEnabled();

    boolean isEnabled(int position);
}
