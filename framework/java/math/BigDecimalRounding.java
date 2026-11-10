/*
 *  Licensed to the Apache Software Foundation (ASF) under one or more
 *  contributor license agreements.  See the NOTICE file distributed with
 *  this work for additional information regarding copyright ownership.
 *  The ASF licenses this file to You under the Apache License, Version 2.0
 *  (the "License"); you may not use this file except in compliance with
 *  the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

package java.math;

final class BigDecimalRounding {
    private BigDecimalRounding() {}

    static int compareAbsoluteValues(long value1, long value2) {
        // Map long values to the range -1 .. Long.MAX_VALUE so that comparison
        // of absolute magnitude can be done using regular long arithmetics.
        // This deals correctly with Long.MIN_VALUE, whose absolute magnitude
        // is larger than any other long value, and which is mapped to
        // Long.MAX_VALUE here.
        // Values that only differ by sign get mapped to the same value, for
        // example both +3 and -3 get mapped to +2.
        value1 = Math.abs(value1) - 1;
        value2 = Math.abs(value2) - 1;
        // Unlike Long.compare(), we guarantee to return specifically -1 and +1
        return value1 > value2 ? 1 : (value1 < value2 ? -1 : 0);
    }

    static int compareForRounding(long n, long d) {
        long halfD = d / 2; //  rounds towards 0
        if (n == halfD || n == -halfD) {
            // In absolute terms: Because n == halfD, we know that 2 * n + lsb == d
            // for some lsb value 0 or 1. This means that n == d/2 (result 0) if
            // lsb is 0, or n < d/2 (result -1) if lsb is 1. In either case, the
            // result is -lsb.
            // Since we're calculating in absolute terms, we need the absolute lsb
            // (d & 1) as opposed to the signed lsb (d % 2) which would be -1 for
            // negative odd values of d.
            int lsb = (int) d & 1;
            return -lsb; // returns 0 or -1
        } else {
            // In absolute terms, either 2 * n + 1 < d (in the case of n < halfD),
            // or 2 * n > d (in the case of n > halfD).
            // In either case, comparing n against halfD gets the right result
            // -1 or +1, respectively.
            return compareAbsoluteValues(n, halfD);
        }
    }

    static int roundingBehavior(int parityBit, int fraction, RoundingMode roundingMode) {
        int increment = 0; // the carry after rounding

        switch (roundingMode) {
            case UNNECESSARY:
                if (fraction != 0) {
                    throw new ArithmeticException("Rounding necessary");
                }
                break;
            case UP:
                increment = Integer.signum(fraction);
                break;
            case DOWN:
                break;
            case CEILING:
                increment = Math.max(Integer.signum(fraction), 0);
                break;
            case FLOOR:
                increment = Math.min(Integer.signum(fraction), 0);
                break;
            case HALF_UP:
                if (Math.abs(fraction) >= 5) {
                    increment = Integer.signum(fraction);
                }
                break;
            case HALF_DOWN:
                if (Math.abs(fraction) > 5) {
                    increment = Integer.signum(fraction);
                }
                break;
            case HALF_EVEN:
                if (Math.abs(fraction) + parityBit > 5) {
                    increment = Integer.signum(fraction);
                }
                break;
        }
        return increment;
    }

    static int safeLongToInt(long longValue) {
        if (longValue < Integer.MIN_VALUE || longValue > Integer.MAX_VALUE) {
            throw new ArithmeticException("Out of int range: " + longValue);
        }
        return (int) longValue;
    }

    static int bitLength(long smallValue) {
        if(smallValue < 0) {
            smallValue = ~smallValue;
        }
        return 64 - Long.numberOfLeadingZeros(smallValue);
    }

    static int bitLength(int smallValue) {
        if(smallValue < 0) {
            smallValue = ~smallValue;
        }
        return 32 - Integer.numberOfLeadingZeros(smallValue);
    }
}
