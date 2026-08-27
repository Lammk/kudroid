package android.text;

/**
 * android.text.TextWatcher — watch for TextView/EditText content changes.
 */
public interface TextWatcher {
    void beforeTextChanged(CharSequence s, int start, int count, int after);

    void onTextChanged(CharSequence s, int start, int before, int count);

    void afterTextChanged(Editable s);
}
