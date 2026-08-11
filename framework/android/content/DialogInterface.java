package android.content;

/**
 * Minimal android.content.DialogInterface implementation.
 *
 * Interface for dialogs. For KuDroid's minimal framework, provides the button
 * constants and click listener.
 */
public interface DialogInterface {
    /** Button: positive. */
    public static final int BUTTON_POSITIVE = -1;
    /** Button: negative. */
    public static final int BUTTON_NEGATIVE = -2;
    /** Button: neutral. */
    public static final int BUTTON_NEUTRAL = -3;

    /**
     * Interface for click callbacks.
     */
    public interface OnClickListener {
        void onClick(DialogInterface dialog, int which);
    }

    /**
     * Interface for dismiss callbacks.
     */
    public interface OnDismissListener {
        void onDismiss(DialogInterface dialog);
    }

    /**
     * Interface for cancel callbacks.
     */
    public interface OnCancelListener {
        void onCancel(DialogInterface dialog);
    }

    /**
     * Dismiss the dialog.
     */
    void dismiss();

    /**
     * Cancel the dialog.
     */
    void cancel();
}
