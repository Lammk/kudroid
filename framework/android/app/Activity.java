package android.app;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;

/**
 * Minimal android.app.Activity implementation.
 *
 * Provides the lifecycle callbacks that native games expect. For KuDroid's
 * minimal framework, the lifecycle methods are no-ops that apps can override.
 */
public class Activity extends ContextThemeWrapper {
    private boolean mCreated = false;
    private boolean mStarted = false;
    private boolean mResumed = false;

    public Activity() {
    }

    /**
     * Called when the activity is first created.
     */
    protected void onCreate(Bundle savedInstanceState) {
    }

    /**
     * Called when the activity is about to become visible.
     */
    protected void onStart() {
    }

    /**
     * Called when the activity has become visible.
     */
    protected void onResume() {
    }

    /**
     * Called when the activity is about to be paused.
     */
    protected void onPause() {
    }

    /**
     * Called when the activity is no longer visible.
     */
    protected void onStop() {
    }

    /**
     * Called before the activity is destroyed.
     */
    protected void onDestroy() {
    }

    /**
     * Called when the activity is restarted.
     */
    protected void onRestart() {
    }

    /**
     * Called when the activity result is available.
     */
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
    }

    /**
     * Called when a new intent is delivered.
     */
    protected void onNewIntent(Intent intent) {
    }

    /**
     * Called when the activity is created (invoked by the framework).
     */
    public void performCreate(Bundle savedInstanceState) {
        mCreated = true;
        onCreate(savedInstanceState);
    }

    /**
     * Called when the activity is started (invoked by the framework).
     */
    public void performStart() {
        mStarted = true;
        onStart();
    }

    /**
     * Called when the activity is resumed (invoked by the framework).
     */
    public void performResume() {
        mResumed = true;
        onResume();
    }

    /**
     * Called when the activity is paused (invoked by the framework).
     */
    public void performPause() {
        mResumed = false;
        onPause();
    }

    /**
     * Called when the activity is stopped (invoked by the framework).
     */
    public void performStop() {
        mStarted = false;
        onStop();
    }

    /**
     * Called when the activity is destroyed (invoked by the framework).
     */
    public void performDestroy() {
        mCreated = false;
        onDestroy();
    }

    /**
     * Return whether the activity has been created.
     */
    public boolean isCreated() {
        return mCreated;
    }

    /**
     * Return whether the activity has been started.
     */
    public boolean isStarted() {
        return mStarted;
    }

    /**
     * Return whether the activity has been resumed.
     */
    public boolean isResumed() {
        return mResumed;
    }

    /**
     * Finish the activity.
     */
    public void finish() {
    }

    /**
     * Return the intent that started this activity.
     */
    public Intent getIntent() {
        return new Intent();
    }

    /**
     * Set the result of this activity.
     */
    public void setResult(int resultCode) {
    }

    /**
     * Set the result of this activity with data.
     */
    public void setResult(int resultCode, Intent data) {
    }

    /**
     * Return the window.
     */
    public android.view.Window getWindow() {
        return new android.view.Window(this);
    }

    /**
     * Set the content view from a layout resource.
     */
    public void setContentView(int layoutResID) {
    }

    /**
     * Set the content view to a view.
     */
    public void setContentView(android.view.View view) {
    }

    /**
     * Find a view by id.
     */
    public android.view.View findViewById(int id) {
        return null;
    }

    /**
     * Run on the UI thread.
     */
    public void runOnUiThread(Runnable action) {
        action.run();
    }
}
