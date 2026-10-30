package android.view.textclassifier;

public interface TextClassifier {
    /** A classifier that never classifies anything. */
    TextClassifier NO_OP = new TextClassifier() {};
}
