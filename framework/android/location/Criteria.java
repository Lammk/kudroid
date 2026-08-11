package android.location;

/**
 * Stub android.location.Criteria.
 *
 * Describes criteria for selecting a location provider. For KuDroid's minimal
 * framework, this is a stub.
 */
public class Criteria {
    /** Accuracy: no requirement. */
    public static final int NO_REQUIREMENT = 0;
    /** Accuracy: low. */
    public static final int ACCURACY_LOW = 1;
    /** Accuracy: medium. */
    public static final int ACCURACY_MEDIUM = 2;
    /** Accuracy: high. */
    public static final int ACCURACY_HIGH = 3;

    /** Power: no requirement. */
    public static final int POWER_LOW = 1;
    /** Power: medium. */
    public static final int POWER_MEDIUM = 2;
    /** Power: high. */
    public static final int POWER_HIGH = 3;

    private int mAccuracy = NO_REQUIREMENT;
    private int mPowerRequirement = NO_REQUIREMENT;

    public Criteria() {
    }

    public Criteria(Criteria criteria) {
        if (criteria != null) {
            mAccuracy = criteria.mAccuracy;
            mPowerRequirement = criteria.mPowerRequirement;
        }
    }

    public int getAccuracy() {
        return mAccuracy;
    }

    public void setAccuracy(int accuracy) {
        mAccuracy = accuracy;
    }

    public int getPowerRequirement() {
        return mPowerRequirement;
    }

    public void setPowerRequirement(int powerRequirement) {
        mPowerRequirement = powerRequirement;
    }
}