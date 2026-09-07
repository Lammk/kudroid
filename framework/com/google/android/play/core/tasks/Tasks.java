package com.google.android.play.core.tasks;

public final class Tasks {
    private Tasks() {}

    public static <ResultT> Task<ResultT> forResult(final ResultT result) {
        return new Task<ResultT>() {
            public boolean isComplete() { return true; }
            public boolean isSuccessful() { return true; }
            public ResultT getResult() { return result; }
            public Exception getException() { return null; }
            public Task<ResultT> addOnCompleteListener(OnCompleteListener<ResultT> listener) {
                if (listener != null) listener.onComplete(this);
                return this;
            }
            public Task<ResultT> addOnSuccessListener(OnSuccessListener<? super ResultT> listener) {
                if (listener != null) listener.onSuccess(result);
                return this;
            }
            public Task<ResultT> addOnFailureListener(OnFailureListener listener) {
                return this;
            }
        };
    }

    public static <ResultT> Task<ResultT> forException(final Exception exception) {
        return new Task<ResultT>() {
            public boolean isComplete() { return true; }
            public boolean isSuccessful() { return false; }
            public ResultT getResult() { return null; }
            public Exception getException() { return exception; }
            public Task<ResultT> addOnCompleteListener(OnCompleteListener<ResultT> listener) {
                if (listener != null) listener.onComplete(this);
                return this;
            }
            public Task<ResultT> addOnSuccessListener(OnSuccessListener<? super ResultT> listener) {
                return this;
            }
            public Task<ResultT> addOnFailureListener(OnFailureListener listener) {
                if (listener != null) listener.onFailure(exception);
                return this;
            }
        };
    }
}
