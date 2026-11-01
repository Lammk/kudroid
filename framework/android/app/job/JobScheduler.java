package android.app.job;

public class JobScheduler {
    public static final int RESULT_FAILURE = 0;
    public static final int RESULT_SUCCESS = 1;
    public JobScheduler() {}

    public java.util.List<JobInfo> getAllPendingJobs() {
        return new java.util.ArrayList<JobInfo>();
    }
    public JobInfo getPendingJob(int id) { return null; }
    public int schedule(JobInfo job) {
        if (job == null) throw new NullPointerException("job");
        // No background job execution backend is available.
        return RESULT_FAILURE;
    }
    public void cancel(int id) {}
    public void cancelAll() {}
}
