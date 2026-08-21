#!/usr/bin/env bash
set -e
mkdir -p include/kudroid
xxd -i framework/build/framework.jar > include/kudroid/framework_jar_bytes.h
echo "✔ Generated include/kudroid/framework_jar_bytes.h ($(wc -c < framework/build/framework.jar) bytes)"
