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

package org.apache.harmony.regex.internal.nls;

/**
 * Message lookup for java.util.regex, ported from Apache Harmony.
 *
 * Harmony's original loads localised text from a ResourceBundle. KuDroid has no
 * resource-bundle machinery and these strings only ever appear inside
 * PatternSyntaxException, so the keys are mapped to plain English here. That keeps
 * the regex sources byte-identical to upstream — the only alternative was editing
 * 40 call sites across 7 files, which would make future updates from Harmony
 * painful for no benefit.
 *
 * Unknown keys return the key itself, so a message added by a later Harmony
 * revision degrades to something searchable rather than throwing.
 */
public class Messages {

    public static String getString(String key) {
        final String msg = lookup(key);
        return msg != null ? msg : key;
    }

    public static String getString(String key, Object arg) {
        return format(getString(key), new Object[] { arg });
    }

    public static String getString(String key, Object arg1, Object arg2) {
        return format(getString(key), new Object[] { arg1, arg2 });
    }

    public static String getString(String key, Object arg1, Object arg2, Object arg3) {
        return format(getString(key), new Object[] { arg1, arg2, arg3 });
    }

    /**
     * Substitute {0}, {1}, ... with the given arguments.
     *
     * A deliberately small subset of java.text.MessageFormat: only positional
     * placeholders, no format types or locale-dependent styles. Harmony's regex
     * messages never use anything else.
     */
    private static String format(String pattern, Object[] args) {
        if (pattern == null) return "";
        StringBuilder out = new StringBuilder(pattern.length() + 32);
        int i = 0;
        while (i < pattern.length()) {
            final char c = pattern.charAt(i);
            if (c != '{') {
                out.append(c);
                i++;
                continue;
            }
            final int close = pattern.indexOf('}', i + 1);
            if (close < 0) {
                out.append(pattern.substring(i));
                break;
            }
            final String body = pattern.substring(i + 1, close);
            int index = -1;
            try {
                index = Integer.parseInt(body.trim());
            } catch (Throwable ignored) {}
            if (index >= 0 && args != null && index < args.length) {
                out.append(args[index] == null ? "null" : args[index].toString());
            } else {
                // Not a placeholder we understand: keep it verbatim.
                out.append(pattern, i, close + 1);
            }
            i = close + 1;
        }
        return out.toString();
    }

    /** Keys as used by Harmony's java.util.regex sources. */
    private static String lookup(String key) {
        if (key == null) return null;
        if (key.equals("regex.00")) return "Inconsistent group index";
        if (key.equals("regex.01")) return "No match found";
        if (key.equals("regex.02")) return "Illegal repetition";
        if (key.equals("regex.03")) return "Illegal group reference";
        if (key.equals("regex.04")) return "Illegal pattern argument";
        if (key.equals("regex.05")) return "Attempt to use a null pattern";
        if (key.equals("regex.06")) return "Illegal char sequence argument";
        if (key.equals("regex.07")) return "Illegal region";
        if (key.equals("regex.08")) return "Illegal char class syntax";
        if (key.equals("regex.09")) return "Illegal quantifier";
        if (key.equals("regex.0A")) return "Illegal escape sequence";
        if (key.equals("regex.0B")) return "Illegal Unicode escape";
        if (key.equals("regex.0C")) return "Illegal hexadecimal escape";
        if (key.equals("regex.0D")) return "Illegal octal escape";
        if (key.equals("regex.0E")) return "Illegal control escape";
        if (key.equals("regex.0F")) return "Illegal character range";
        if (key.equals("regex.10")) return "Syntax error in pattern near index {1}: {0}";
        if (key.equals("regex.11")) return "Illegal char class construction";
        if (key.equals("regex.12")) return "Unmatched closing parenthesis";
        if (key.equals("regex.13")) return "Unmatched opening parenthesis";
        if (key.equals("regex.14")) return "Unmatched closing bracket";
        if (key.equals("regex.15")) return "Unexpected end of pattern: {0}";
        if (key.equals("regex.16")) return "Illegal pattern: {0} at index {1}";
        if (key.equals("regex.17")) return "Illegal pattern: {0}";
        if (key.equals("regex.18")) return "Illegal {0} escape sequence";
        if (key.equals("regex.19")) return "Illegal repetition range";
        if (key.equals("regex.1A")) return "Illegal group name";
        if (key.equals("regex.1B")) return "Illegal back reference";
        if (key.equals("regex.1C")) return "Illegal look-behind construction";
        if (key.equals("regex.1D")) return "Unsupported construction";
        if (key.equals("regex.1E")) return "Illegal flag";
        if (key.equals("regex.1F")) return "Unknown character class name";
        if (key.equals("regex.20")) return "Illegal quantifier target";
        if (key.equals("regex.21")) return "Empty alternative";
        if (key.equals("regex.22")) return "Illegal named group reference";
        return null;
    }
}
