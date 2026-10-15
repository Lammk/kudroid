#!/usr/bin/env bash
# Summarize a KuDroid logs/ capture: frame pacing, render cost, I/O stalls,
# audio backlog, jar misses. Read-only; run after each on-device test round.
# Usage: scripts/analyze_logs.sh [logs-dir]   (default: ./logs)
set -euo pipefail

DIR="${1:-./logs}"
A="$DIR/kudroid_android_logs.txt"
B="$DIR/native_breadcrumbs.log"

say() { printf '\n== %s ==\n' "$1"; }

say "pacing (deltas per 5s telemetry line; 60 = full rate)"
if [ -f "$A" ]; then
    grep -h "frames: looper=" "$A" | sed 's/.*looper=\([0-9]*\) direct=\([0-9]*\).*/\1 \2/' \
    | awk 'NR>1{dl=$1-pl; dd=$2-pd; if(dl>maxl)maxl=dl; if(dd>maxd)maxd=dd}
           {pl=$1; pd=$2}
           END{printf "  max looper-delta=%d  max direct-delta=%d\n", maxl, maxd}'
    grep -h "frames: looper=" "$A" | tail -1 | sed 's/^/  last: /'
    grep -h "mode=" "$A" | sed 's/.*mode=//' | sort -u | sed 's/^/  modes seen: /'
    grep -h "latch\|direct mode" "$A" | tail -3 | sed 's/^/  /' || true
else
    echo "  (missing $A)"
fi

say "nativeRender duration (ms)"
if [ -f "$B" ]; then
    grep -h "native-exit" "$B" | grep "method=nativeRender" \
    | grep -oE "duration_ms=[0-9]+" | cut -d= -f2 | sort -n \
    | awk '{a[NR]=$1} END{if(NR==0)print "  (none)"; else printf "  n=%d  p50=%d  p95=%d  max=%d\n", NR, a[int(NR*0.5)], a[int(NR*0.95)+1], a[NR]}'
    echo -n "  last breadcrumb: "; tail -1 "$B" | cut -c1-140
else
    echo "  (missing $B)"
fi

say "frame census (swaps delta between census lines)"
[ -f "$A" ] && grep -h "frame census" "$A" | sed 's/.*swaps=\([0-9]*\).*/\1/' \
| awk 'NR>1{d=$1-p; if(d>max)max=d} {p=$1} END{if(NR)printf "  census lines=%d  max swaps-delta=%d\n", NR, max}' \
|| echo "  (none)"

say "stalls / misses"
[ -f "$A" ] && { c=$(grep -c "jar miss" "$A" || true); echo "  jar miss: $c"; } || true
[ -f "$A" ] && grep -h "slow pread64" "$A" | tail -3 | sed 's/^/  /' || true
[ -f "$A" ] && grep -h "eglSwapBuffers slow" "$A" | tail -3 | sed 's/^/  /' || true

say "audio (last in-flight)"
[ -f "$A" ] && grep -h "in-flight=" "$A" | tail -1 | sed 's/^/  /' || echo "  (none)"

say "guest progress markers"
[ -f "$A" ] && grep -h "Bootstrap:Start\|catalog" "$A" | tail -3 | sed 's/^/  /' || echo "  (none)"

say "crash signals"
[ -f "$A" ] && grep -hE "FATAL|SIGSEGV|SIGABRT|tombstone" "$A" | tail -3 | sed 's/^/  /' || echo "  (none)"
