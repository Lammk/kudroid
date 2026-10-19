package java.lang;

public class StackTraceElement {

    private String declaringClass;
    private String methodName;
    private String fileName;
    private int lineNumber;

    public StackTraceElement(String declaringClass, String methodName, String fileName,
            int lineNumber) {
        if (declaringClass == null) throw new NullPointerException("Declaring class is null");
        if (methodName == null) throw new NullPointerException("Method name is null");
        this.declaringClass = declaringClass;
        this.methodName = methodName;
        this.fileName = fileName;
        this.lineNumber = lineNumber;
    }

    public String getClassName() {
        return declaringClass;
    }

    public String getMethodName() {
        return methodName;
    }

    public String getFileName() {
        return fileName;
    }

    public int getLineNumber() {
        return lineNumber;
    }

    public boolean isNativeMethod() {
        return lineNumber == -2;
    }

    public boolean equals(Object other) {
        if (other == this) return true;
        if (!(other instanceof StackTraceElement)) return false;
        StackTraceElement e = (StackTraceElement) other;
        return lineNumber == e.lineNumber
                && declaringClass.equals(e.declaringClass)
                && methodName.equals(e.methodName)
                && (fileName == null ? e.fileName == null : fileName.equals(e.fileName));
    }

    public int hashCode() {
        int result = 31 * declaringClass.hashCode() + methodName.hashCode();
        result = 31 * result + (fileName == null ? 0 : fileName.hashCode());
        return 31 * result + lineNumber;
    }

    public String toString() {
        StringBuilder sb = new StringBuilder(80);
        sb.append(declaringClass).append('.').append(methodName).append('(');
        if (isNativeMethod()) {
            sb.append("Native Method");
        } else if (fileName != null) {
            sb.append(fileName);
            if (lineNumber >= 0) sb.append(':').append(lineNumber);
        } else {
            sb.append("Unknown Source");
        }
        return sb.append(')').toString();
    }
}
