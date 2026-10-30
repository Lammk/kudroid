/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package java.math;

import java.util.Arrays;

/** Mutable signed magnitude arithmetic without native allocations. */
final class BigInt {
    private int[] words;
    private int sign;
    private static final long MASK = 0xffffffffL;

    boolean hasNativeBignum() { return words != null; }
    private void set(int[] value, int polarity) {
        int length = value.length;
        while (length > 1 && value[length - 1] == 0) length--;
        words = length == 0 ? new int[] {0} : Arrays.copyOf(value, length);
        sign = words.length == 1 && words[0] == 0 ? 0 : polarity < 0 ? -1 : 1;
    }
    private static BigInt of(int[] value, int polarity) {
        BigInt result = new BigInt(); result.set(value, polarity); return result;
    }
    private static BigInt small(long value) { BigInt result = new BigInt(); result.putLongInt(value); return result; }
    void putCopy(BigInt value) { set(value.words, value.sign); }
    BigInt copy() { return of(words, sign); }
    void putLongInt(long value) { putULongInt(value < 0 ? -value : value, value < 0); }
    void putULongInt(long value, boolean negative) { set(new int[] {(int) value, (int) (value >>> 32)}, negative ? -1 : 1); }
    String checkString(String text, int radix) {
        if (text == null) throw new NullPointerException();
        if (text.length() == 0) throw new NumberFormatException("Empty integer");
        StringBuffer ascii = new StringBuffer();
        int start = text.charAt(0) == '+' || text.charAt(0) == '-' ? 1 : 0;
        if (start == text.length()) throw new NumberFormatException("Missing integer digits");
        if (text.charAt(0) == '-') ascii.append('-');
        for (int i = start; i < text.length(); i++) {
            int digit = Character.digit(text.charAt(i), radix);
            if (digit < 0) throw new NumberFormatException("Invalid integer digit");
            ascii.append(Character.forDigit(digit, radix));
        }
        return ascii.toString();
    }
    private void putString(String text, int radix) {
        String ascii = checkString(text, radix);
        boolean negative = ascii.charAt(0) == '-';
        putLongInt(0);
        for (int i = negative ? 1 : 0; i < ascii.length(); i++) {
            multiplyByPositiveInt(radix); addPositiveInt(Character.digit(ascii.charAt(i), radix));
        }
        if (negative) setSign(-1);
    }
    void putDecString(String text) { putString(text, 10); }
    void putHexString(String text) { putString(text, 16); }
    void putBigEndian(byte[] bytes, boolean negative) {
        int[] value = new int[(bytes.length + 3) / 4];
        for (int i = 0; i < bytes.length; i++) value[i / 4] |= (bytes[bytes.length - 1 - i] & 255) << (i % 4 * 8);
        set(value, negative ? -1 : 1);
    }
    void putLittleEndianInts(int[] value, boolean negative) { set(value, negative ? -1 : 1); }
    void putBigEndianTwosComplement(byte[] bytes) {
        if (bytes.length == 0) throw new NumberFormatException("Empty integer");
        if (bytes[0] >= 0) { putBigEndian(bytes, false); return; }
        byte[] magnitude = new byte[bytes.length]; int carry = 1;
        for (int i = bytes.length - 1; i >= 0; i--) {
            int value = (bytes[i] ^ 255) & 255; value += carry;
            magnitude[i] = (byte) value; carry = value >>> 8;
        }
        putBigEndian(magnitude, true);
    }
    long longInt() {
        long value = words[0] & MASK;
        if (words.length > 1) value |= (long) words[1] << 32;
        return sign < 0 ? -value : value;
    }
    private String radixString(int radix) {
        if (sign == 0) return "0";
        BigInt remaining = copy(); remaining.sign = 1;
        StringBuffer reversed = new StringBuffer();
        BigInt divisor = small(radix);
        while (remaining.sign != 0) {
            BigInt quotient = new BigInt(), remainder = new BigInt();
            division(remaining, divisor, quotient, remainder);
            reversed.append(Character.forDigit((int) remainder.longInt(), radix)); remaining = quotient;
        }
        if (sign < 0) reversed.append('-');
        return reversed.reverse().toString();
    }
    String decString() { return radixString(10); }
    String hexString() { return radixString(16); }
    @Override public String toString() { return decString(); }
    byte[] bigEndianMagnitude() {
        byte[] bytes = new byte[(magnitudeBitLength() + 7) / 8];
        for (int i = 0; i < bytes.length; i++) bytes[bytes.length - 1 - i] = (byte) (words[i / 4] >>> (i % 4 * 8));
        return bytes;
    }
    int[] littleEndianIntsMagnitude() { return words.clone(); }
    int sign() { return sign; }
    void setSign(int value) { if (sign != 0 && value != 0) sign = value < 0 ? -1 : 1; }
    boolean twosCompFitsIntoBytes(int count) { return (magnitudeBitLength() + 7) / 8 <= count; }
    private int magnitudeBitLength() {
        return sign == 0 ? 0 : (words.length - 1) * 32 + 32 - Integer.numberOfLeadingZeros(words[words.length - 1]);
    }
    int bitLength() {
        int bits = magnitudeBitLength();
        if (sign < 0) {
            int nonzero = 0;
            for (int word : words) nonzero += Integer.bitCount(word);
            if (nonzero == 1) bits--;
        }
        return bits;
    }
    boolean isBitSet(int bit) { return bit >= 0 && bit / 32 < words.length && (words[bit / 32] & (1 << (bit % 32))) != 0; }
    private static int compareMagnitude(BigInt a, BigInt b) {
        if (a.words.length != b.words.length) return a.words.length < b.words.length ? -1 : 1;
        for (int i = a.words.length - 1; i >= 0; i--) {
            long x = a.words[i] & MASK, y = b.words[i] & MASK;
            if (x != y) return x < y ? -1 : 1;
        }
        return 0;
    }
    static int cmp(BigInt a, BigInt b) {
        if (a.sign != b.sign) return a.sign < b.sign ? -1 : 1;
        return a.sign == 0 ? 0 : a.sign * compareMagnitude(a, b);
    }
    private static BigInt sumMagnitude(BigInt a, BigInt b, int sign) {
        int[] value = new int[Math.max(a.words.length, b.words.length) + 1]; long carry = 0;
        for (int i = 0; i < value.length - 1; i++) {
            long sum = carry + (i < a.words.length ? a.words[i] & MASK : 0) + (i < b.words.length ? b.words[i] & MASK : 0);
            value[i] = (int) sum; carry = sum >>> 32;
        }
        value[value.length - 1] = (int) carry; return of(value, sign);
    }
    private static BigInt differenceMagnitude(BigInt a, BigInt b, int sign) {
        int[] value = new int[a.words.length]; long borrow = 0;
        for (int i = 0; i < value.length; i++) {
            long difference = (a.words[i] & MASK) - (i < b.words.length ? b.words[i] & MASK : 0) - borrow;
            value[i] = (int) difference; borrow = difference < 0 ? 1 : 0;
        }
        return of(value, sign);
    }
    static BigInt addition(BigInt a, BigInt b) {
        if (a.sign == 0) return b.copy(); if (b.sign == 0) return a.copy();
        if (a.sign == b.sign) return sumMagnitude(a, b, a.sign);
        int comparison = compareMagnitude(a, b);
        return comparison >= 0 ? differenceMagnitude(a, b, a.sign) : differenceMagnitude(b, a, b.sign);
    }
    static BigInt subtraction(BigInt a, BigInt b) { BigInt negative = b.copy(); negative.sign = -negative.sign; return addition(a, negative); }
    void add(BigInt value) { putCopy(addition(this, value)); }
    void addPositiveInt(int value) { BigInt positive = new BigInt(); positive.putULongInt(value & MASK, false); add(positive); }
    static BigInt product(BigInt a, BigInt b) {
        if (a.sign == 0 || b.sign == 0) return small(0);
        int[] value = new int[a.words.length + b.words.length];
        for (int i = 0; i < a.words.length; i++) {
            long carry = 0;
            for (int j = 0; j < b.words.length; j++) {
                long total = (a.words[i] & MASK) * (b.words[j] & MASK) + (value[i + j] & MASK) + carry;
                value[i + j] = (int) total; carry = total >>> 32;
            }
            value[i + b.words.length] = (int) carry;
        }
        return of(value, a.sign * b.sign);
    }
    void multiplyByPositiveInt(int value) { BigInt factor = new BigInt(); factor.putULongInt(value & MASK, false); putCopy(product(this, factor)); }
    static BigInt shift(BigInt a, int count) {
        if (count == 0 || a.sign == 0) return a.copy();
        if (count == Integer.MIN_VALUE) return small(0);
        int amount = Math.abs(count), whole = amount / 32, bits = amount % 32;
        if (count > 0) {
            int[] value = new int[a.words.length + whole + (bits == 0 ? 0 : 1)];
            for (int i = 0; i < a.words.length; i++) {
                value[i + whole] |= a.words[i] << bits;
                if (bits != 0) value[i + whole + 1] |= a.words[i] >>> (32 - bits);
            }
            return of(value, a.sign);
        }
        if (whole >= a.words.length) return small(0);
        int[] value = new int[a.words.length - whole];
        for (int i = 0; i < value.length; i++) {
            value[i] = a.words[i + whole] >>> bits;
            if (bits != 0 && i + whole + 1 < a.words.length) value[i] |= a.words[i + whole + 1] << (32 - bits);
        }
        return of(value, a.sign);
    }
    void shift(int count) { putCopy(shift(this, count)); }
    static void division(BigInt dividend, BigInt divisor, BigInt quotient, BigInt remainder) {
        if (divisor.sign == 0) throw new ArithmeticException("Division by zero");
        BigInt left = dividend.copy(); left.sign = left.sign == 0 ? 0 : 1;
        BigInt right = divisor.copy(); right.sign = 1;
        BigInt q = small(0), r = left;
        if (right.words.length == 1 && (right.words[0] & MASK) <= 0x7fffffffL) {
            long rem = 0, divisorWord = right.words[0] & MASK;
            int[] value = new int[left.words.length];
            for (int i = value.length - 1; i >= 0; i--) {
                long current = (rem << 32) | (left.words[i] & MASK);
                value[i] = (int) (current / divisorWord); rem = current % divisorWord;
            }
            q = of(value, dividend.sign * divisor.sign); r = small(rem); r.setSign(dividend.sign);
        } else if (compareMagnitude(left, right) >= 0) {
            int offset = left.magnitudeBitLength() - right.magnitudeBitLength();
            BigInt shifted = shift(right, offset);
            int[] value = new int[offset / 32 + 1];
            for (int bit = offset; bit >= 0; bit--) {
                if (compareMagnitude(r, shifted) >= 0) {
                    r = differenceMagnitude(r, shifted, 1); value[bit / 32] |= 1 << (bit % 32);
                }
                shifted.shift(-1);
            }
            q = of(value, dividend.sign * divisor.sign); r.setSign(dividend.sign);
        } else r.setSign(dividend.sign);
        if (quotient != null) quotient.putCopy(q);
        if (remainder != null) remainder.putCopy(r);
    }
    static int remainderByPositiveInt(BigInt a, int value) {
        BigInt divisor = new BigInt(), remainder = new BigInt(); divisor.putULongInt(value & MASK, false);
        division(a, divisor, null, remainder); return (int) Math.abs(remainder.longInt());
    }
    static BigInt gcd(BigInt a, BigInt b) {
        BigInt x = a.copy(), y = b.copy(); x.setSign(1); y.setSign(1);
        while (y.sign != 0) { BigInt remainder = new BigInt(); division(x, y, null, remainder); x = y; y = remainder; }
        return x;
    }
    static BigInt bigExp(BigInt a, BigInt power) {
        BigInt result = small(1), base = a.copy();
        for (int bit = 0; bit < power.magnitudeBitLength(); bit++) {
            if (power.isBitSet(bit)) result = product(result, base);
            if (bit + 1 < power.magnitudeBitLength()) base = product(base, base);
        }
        return result;
    }
    static BigInt exp(BigInt a, int power) { return bigExp(a, small(power)); }
    static BigInt modulus(BigInt a, BigInt modulus) {
        if (modulus.sign <= 0) throw new ArithmeticException("Nonpositive modulus");
        BigInt remainder = new BigInt(); division(a, modulus, null, remainder);
        return remainder.sign < 0 ? addition(remainder, modulus) : remainder;
    }
    static BigInt modExp(BigInt a, BigInt power, BigInt modulus) {
        BigInt result = modulus(small(1), modulus), base = modulus(a, modulus);
        for (int bit = 0; bit < power.magnitudeBitLength(); bit++) {
            if (power.isBitSet(bit)) result = modulus(product(result, base), modulus);
            if (bit + 1 < power.magnitudeBitLength()) base = modulus(product(base, base), modulus);
        }
        return result;
    }
    static BigInt modInverse(BigInt a, BigInt modulus) {
        if (modulus.sign <= 0) throw new ArithmeticException("Nonpositive modulus");
        BigInt r = modulus.copy(), nextR = modulus(a, modulus), t = small(0), nextT = small(1);
        while (nextR.sign != 0) {
            BigInt quotient = new BigInt(), remainder = new BigInt(); division(r, nextR, quotient, remainder);
            BigInt coefficient = subtraction(t, product(quotient, nextT)); t = nextT; nextT = coefficient;
            r = nextR; nextR = remainder;
        }
        if (cmp(r, small(1)) != 0) throw new ArithmeticException("Integer is not invertible");
        return modulus(t, modulus);
    }
    static BigInt generatePrimeDefault(int bits) {
        long handle = NativeBN.BN_new();
        if (handle == 0) throw new UnsupportedOperationException("Native prime generation unavailable");
        try {
            NativeBN.BN_generate_prime_ex(handle, bits, false, 0, 0);
            BigInt result = new BigInt(); result.putBigEndian(NativeBN.BN_bn2bin(handle), false); return result;
        } finally { NativeBN.BN_free(handle); }
    }
    boolean isPrime(int certainty) {
        long handle = NativeBN.BN_new();
        if (handle == 0) throw new UnsupportedOperationException("Native primality testing unavailable");
        try {
            byte[] magnitude = bigEndianMagnitude(); NativeBN.BN_bin2bn(magnitude, magnitude.length, false, handle);
            return NativeBN.BN_primality_test(handle, certainty, false);
        } finally { NativeBN.BN_free(handle); }
    }
}
