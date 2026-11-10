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

package java.util.regex;

/**
 * This class gives us a hashtable that contains canonical
 * decomposition mappings that are generated from
 * http://www.unicode.org/Public/4.0-Update/UnicodeData-4.0.0.txt.
 */
class HashDecompositions{

    private static IntArrHash table = null;

    private HashDecompositions() {
    }

    public static IntArrHash getHashDecompositions() {
        if (table != null) {
            return table;
        } else {
            table = new IntArrHash(2032);
            HashDecompositionData.populate(table);
            HashDecompositionSupplement.populate(table);
            return table;
        }
    }
}
