package android.content;

/**
 * minimal android.content.dialoginterface implementation.
 *
 * interface for dialog boxes. for kudroid minimal framework, provide constant
 * button and click listener.
 */
public interface DialogInterface {
    /** node: active. */
    public static final int BUTTON_POSITIVE = -1;
    /** node: negative. */
    public static final int BUTTON_NEGATIVE = -2;
    /** node: neutral. */
    public static final int BUTTON_NEUTRAL = -3;

    /**
     * interface for callbacks on click.
     */
    public interface OnClickListener {
        void onClick(DialogInterface dialog, int which);
    }

    /**
     * interface for callbacks when ignored.
     */
    public interface OnDismissListener {
        void onDismiss(DialogInterface dialog);
    }

    /**
     * interface for callbacks on cancellation.
     */
    public interface OnCancelListener {
        void onCancel(DialogInterface dialog);
    }

    /**
     * Callback when the dialog appears.
     */
    public interface OnShowListener {
        void onShow(DialogInterface dialog);
    }

    /**
     * Callback hard key when dialog is showing. Returns true if processed.
     */
    public interface OnKeyListener {
        boolean onKey(DialogInterface dialog, int keyCode, android.view.KeyEvent event);
    }

    /**
     * skip dialog.
     */
    void dismiss();

    /**
     * cancel dialog.
     */
    void cancel();

    public static class OnMultiChoiceClickListener {
        public OnMultiChoiceClickListener() {}
    }

}
