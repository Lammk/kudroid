package android.webkit;

public class JsPromptResult extends JsResult {
    private String mStringResult;

    public void confirm(String result) {
        super.confirm();
        mStringResult = result;
    }

    public String getStringResult() { return mStringResult; }
}
