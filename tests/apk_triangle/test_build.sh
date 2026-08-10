#!/bin/bash
if [ -z "$ANDROID_NDK_HOME" ] && [ -n "$ANDROID_NDK_LATEST_HOME" ]; then
    export ANDROID_NDK_HOME="$ANDROID_NDK_LATEST_HOME"
fi
echo $ANDROID_NDK_HOME
