import android.app.job.JobInfo;
import android.app.job.JobScheduler;
import android.content.Context;
import android.content.ContextWrapper;
import java.util.List;

public class LoadingFrameworkTests {
    private static int checks;
    private static void check(boolean value, String message) {
        checks++;
        if (!value) throw new AssertionError(message);
    }
    public static int run() {
        checks = 0;
        check("https://example.test".regionMatches(true, 0, "https:", 0, 6), "valid HTTPS scheme recognized");
        check("HTTP://example.test".regionMatches(true, 0, "http:", 0, 5), "scheme case ignored");
        check(!"HTTP:".regionMatches(false, 0, "http:", 0, 5), "case sensitive overload");
        check("xxAbCyy".regionMatches(true, 2, "_aBc_", 1, 3), "both region offsets respected");
        check("\u00E9".regionMatches(true, 0, "\u00C9", 0, 1), "Latin simple case");
        check("\u03C2".regionMatches(true, 0, "\u03C3", 0, 1), "Greek final sigma uses upper mapping");
        check("\u0130".regionMatches(true, 0, "i", 0, 1), "dotted I uses lower mapping after uppercase");
        check("\u0131".regionMatches(true, 0, "I", 0, 1), "dotless I uses simple mapping");
        check("\u212A".regionMatches(true, 0, "k", 0, 1), "Kelvin sign uses simple lower mapping");
        check(!"\u00DF".regionMatches(true, 0, "ss", 0, 1), "no multicharacter case expansion");
        check("\uD83D\uDE00a".regionMatches(true, 2, "A", 0, 1), "UTF-16 code unit offset");
        check("\uD83D\uDE00".regionMatches(false, 1, "\uDE00", 0, 1), "surrogate code unit region");
        check(!"abc".regionMatches(true, -1, "abc", 0, 0), "negative offset rejected");
        check(!"abc".regionMatches(true, 0, "abc", -1, 0), "negative other offset rejected");
        check(!"abc".regionMatches(true, 1, "abc", 0, Integer.MAX_VALUE), "overflow-safe region bounds");
        check("abc".regionMatches(true, 3, "abc", 3, 0), "empty region at end");
        check(!"abc".regionMatches(true, 4, "abc", 3, 0), "empty region beyond end rejected");
        check("abc".regionMatches(true, 4, "abc", 4, -1), "negative length follows Java region bounds");
        check("abc".regionMatches(false, Integer.MAX_VALUE, "abc", Integer.MAX_VALUE, Integer.MIN_VALUE),
              "negative length bounds do not overflow");
        try { "abc".regionMatches(true, -1, null, 0, 1); check(false, "null comparison accepted"); }
        catch (NullPointerException expected) { check(true, "null comparison checked before bounds"); }
        check(Character.toUpperCase(0x10428) == 0x10400, "supplementary simple uppercase");
        check(Character.toLowerCase(0x10400) == 0x10428, "supplementary simple lowercase");
        check(Character.toUpperCase(-1) == -1 && Character.toLowerCase(0x110000) == 0x110000,
              "out-of-range code points preserved");

        Context context = new ContextWrapper(null);
        JobScheduler scheduler = (JobScheduler) context.getSystemService(Context.JOB_SCHEDULER_SERVICE);
        check(scheduler != null, "scheduler service exists");
        List<JobInfo> jobs = scheduler.getAllPendingJobs();
        check(jobs != null && jobs.isEmpty() && !jobs.iterator().hasNext(), "pending jobs always a non-null list");
        jobs.add(new JobInfo());
        check(scheduler.getAllPendingJobs().isEmpty(), "caller mutation cannot create scheduled jobs");
        check(((JobScheduler) context.getSystemService(JobScheduler.class)).getAllPendingJobs().isEmpty(), "typed scheduler service agrees");
        android.content.ComponentName service = new android.content.ComponentName("example.test", "example.test.JobService");
        android.os.PersistableBundle extras = new android.os.PersistableBundle();
        extras.putInt("attemptNumber", 3);
        JobInfo.Builder builder = new JobInfo.Builder(42, service).setExtras(extras)
            .setMinimumLatency(1000).setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
            .setRequiresCharging(true).setRequiresDeviceIdle(true);
        JobInfo job = builder.build();
        extras.putInt("attemptNumber", 4);
        builder.setMinimumLatency(2000).setRequiresCharging(false);
        check(job.getId() == 42 && job.getService() == service, "job identity preserved");
        check(job.getExtras().getInt("attemptNumber") == 3, "built extras isolated from builder input");
        check(job.getMinLatencyMillis() == 1000 && job.isRequireCharging() && job.isRequireDeviceIdle() &&
              job.getNetworkType() == JobInfo.NETWORK_TYPE_ANY, "job constraints snapshot builder state");
        check(scheduler.schedule(job) == JobScheduler.RESULT_FAILURE, "unavailable job execution reports failure");
        scheduler.cancel(42); scheduler.cancelAll();
        check(scheduler.getAllPendingJobs().isEmpty() && scheduler.getPendingJob(42) == null,
              "failed or cancelled jobs are never pending");
        try { new JobInfo.Builder(1, service).setRequiredNetworkType(99); check(false, "unknown network type accepted"); }
        catch (IllegalArgumentException expected) { check(true, "unknown network type rejected"); }
        return checks;
    }
}
