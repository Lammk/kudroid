package com.google.android.play.core.tasks;

public abstract class Task<ResultT> {
    public abstract boolean isComplete();
    public abstract boolean isSuccessful();
    public abstract ResultT getResult();
    public abstract Exception getException();
    public abstract Task<ResultT> addOnCompleteListener(OnCompleteListener<ResultT> listener);
    public abstract Task<ResultT> addOnSuccessListener(OnSuccessListener<? super ResultT> listener);
    public abstract Task<ResultT> addOnFailureListener(OnFailureListener listener);
}
