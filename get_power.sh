#!/usr/bin/env bash

# VQ920 / SXR power sampler.
# The stage is supplied by the caller so each CSV is self-describing.
# Example (Git Bash on Windows):
#   "D:/Git/bin/bash.exe" ./get_power.sh --stage idle --duration 300

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STAGE=""
LABEL=""
DURATION=300
INTERVAL=2
ADB=adb

usage() {
    cat <<'EOF'
Usage: get_power.sh --stage <idle|preview|phone_record|local_record> [options]
  --stage NAME       required
  --label NAME       output label, default is stage
  --duration SEC     default 300
  --interval SEC     default 2
  --adb PATH         adb executable
EOF
}

die() { echo "ERROR: $*" >&2; exit 2; }

while [ "$#" -gt 0 ]; do
    case "$1" in
        --stage) [ "$#" -ge 2 ] || die "--stage requires a value"; STAGE="$2"; shift 2 ;;
        --label) [ "$#" -ge 2 ] || die "--label requires a value"; LABEL="$2"; shift 2 ;;
        --duration) [ "$#" -ge 2 ] || die "--duration requires a value"; DURATION="$2"; shift 2 ;;
        --interval) [ "$#" -ge 2 ] || die "--interval requires a value"; INTERVAL="$2"; shift 2 ;;
        --adb) [ "$#" -ge 2 ] || die "--adb requires a value"; ADB="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

[ -n "$STAGE" ] || { usage >&2; exit 2; }
case "$STAGE" in idle|preview|phone_record|local_record) ;; *) die "bad stage: $STAGE" ;; esac
case "$DURATION" in ''|*[!0-9]*) die "duration must be an integer" ;; esac
case "$INTERVAL" in ''|*[!0-9.]*|.*|*.) die "interval must be positive" ;; esac
[ "$(awk -v x="$INTERVAL" 'BEGIN { print (x > 0) ? 0 : 1 }')" -eq 0 ] ||
    die "interval must be positive"

if ! command -v "$ADB" >/dev/null 2>&1 && [ -x /c/adb/adb.exe ]; then ADB=/c/adb/adb.exe; fi
command -v "$ADB" >/dev/null 2>&1 || die "adb not found: $ADB"
[ -n "$LABEL" ] || LABEL="$STAGE"

RUN_ID=$(date -u '+%Y%m%d_%H%M%S')
OUT_DIR="$SCRIPT_DIR/power_tests"
RUN_DIR="$OUT_DIR/$RUN_ID-$LABEL"
mkdir -p "$RUN_DIR"
CSV_FILE="$RUN_DIR/data.csv"
SUMMARY_FILE="$RUN_DIR/summary.txt"

DEVICE_COUNT=$("$ADB" devices 2>/dev/null | awk '$2 == "device" { n++ } END { print n + 0 }')
[ "$DEVICE_COUNT" -eq 1 ] || die "expected one online adb device, found $DEVICE_COUNT"

cat >"$CSV_FILE" <<'CSV'
timestamp_utc,elapsed_s,battery_level_pct,charge_counter_mAh,battery_current_mA,battery_voltage_V,battery_status,usb_online,battery_temp_C,skin_temp_C,cpu_temp_max_C,cpu_temp_avg_C,app_pid,app_cpu_pct,app_rss_kb,app_mem_pct,system_mem_used_kb,system_mem_available_kb,system_cpu_used_pct,cameraserver_cpu_pct,qvrservice_cpu_pct,sxr_service_cpu_pct
CSV

section() {
    printf '%s\n' "$REMOTE" | awk -v a="$1" -v b="$2" '$0 == a { f=1; next } $0 == b { f=0 } f'
}

echo "stage=$STAGE duration=$DURATION seconds interval=$INTERVAL seconds"
echo "csv=$CSV_FILE"
echo "summary=$SUMMARY_FILE"
echo "Sampling..."
SAMPLE_START=$(date +%s)

while :; do
    elapsed=$(($(date +%s) - SAMPLE_START))
    [ "$elapsed" -ge "$DURATION" ] && break
    timestamp=$(date -u '+%Y-%m-%dT%H:%M:%SZ')

    REMOTE=$("$ADB" shell '
        echo __BATTERY__
        dumpsys battery
        echo __POWER__
        cat /sys/class/power_supply/battery/charge_counter
        cat /sys/class/power_supply/battery/current_now
        cat /sys/class/power_supply/battery/voltage_now
        cat /sys/class/power_supply/usb/online
        echo __THERMAL__
        dumpsys thermalservice
        echo __TOP__
        top -b -n 1 -m 30
        echo __MEM__
        cat /proc/meminfo
    ' 2>/dev/null | tr -d '\r')

    BAT=$(section __BATTERY__ __POWER__)
    POWER=$(section __POWER__ __THERMAL__)
    THERMAL=$(section __THERMAL__ __TOP__)
    TOP=$(section __TOP__ __MEM__)
    MEM=$(printf '%s\n' "$REMOTE" | awk '/^__MEM__/{f=1;next} f')

    level=$(printf '%s\n' "$BAT" | awk '/^[[:space:]]*level:/{print $2; exit}')
    status=$(printf '%s\n' "$BAT" | awk '/^[[:space:]]*status:/{print $2; exit}')
    btemp=$(printf '%s\n' "$BAT" | awk '/^[[:space:]]*temperature:/{print $2; exit}')
    charge=$(printf '%s\n' "$POWER" | sed -n '1p')
    current=$(printf '%s\n' "$POWER" | sed -n '2p')
    voltage=$(printf '%s\n' "$POWER" | sed -n '3p')
    usb=$(printf '%s\n' "$POWER" | sed -n '4p')

    skin=$(printf '%s\n' "$THERMAL" | grep -E 'mName=skin' |
        sed -E 's/.*mValue=([^,}]+).*/\1/' | sort -nr | head -1)
    cpus=$(printf '%s\n' "$THERMAL" | grep -E 'mName=CPU[0-9]+' |
        sed -E 's/.*mValue=([^,}]+).*/\1/')
    cpu_max=$(printf '%s\n' "$cpus" | sort -nr | head -1)
    cpu_avg=$(printf '%s\n' "$cpus" | awk '{ s += $1; n++ } END { if (n) printf "%.3f", s/n }')

    cpu_line=$(printf '%s\n' "$TOP" | grep -i -m 1 '%cpu')
    cores=$(printf '%s\n' "$cpu_line" | sed -E 's/^[[:space:]]*([0-9]+)%cpu.*/\1/')
    idle_sum=$(printf '%s\n' "$cpu_line" | grep -oE '[0-9]+%idle' | sed 's/%idle//' | head -1)
    system_cpu=$(awk -v c="$cores" -v i="$idle_sum" 'BEGIN { if (c > 0 && i != "") printf "%.2f", 100-100*i/c }')

    proc=$(printf '%s\n' "$TOP" | grep -m 1 'com.ssnwt.helloxr' 2>/dev/null || true)
    app_pid=$(printf '%s\n' "$proc" | awk '{print $1}')
    app_res=$(printf '%s\n' "$proc" | awk '{print $6}')
    app_cpu=$(printf '%s\n' "$proc" | awk '{print $9}')
    app_mem=$(printf '%s\n' "$proc" | awk '{print $10}')
    app_res_num=$(echo "$app_res" | sed 's/[GMK]$//')
    case "$app_res" in
        *G) app_res_kb=$(awk -v x="$app_res_num" 'BEGIN { printf "%.0f", x*1024*1024 }') ;;
        *M) app_res_kb=$(awk -v x="$app_res_num" 'BEGIN { printf "%.0f", x*1024 }') ;;
        *K) app_res_kb="$app_res_num" ;;
        *) app_res_kb="$app_res" ;;
    esac

    camera_proc=$(printf '%s\n' "$TOP" | grep -i -m 1 'cameraserver' 2>/dev/null || true)
    qvr_proc=$(printf '%s\n' "$TOP" | grep -i -m 1 'qvrservice' 2>/dev/null || true)
    sxr_proc=$(printf '%s\n' "$TOP" | grep -i -m 1 'sxr_service' 2>/dev/null || true)
    camera_cpu=$(printf '%s\n' "$camera_proc" | awk '{print $9}')
    qvr_cpu=$(printf '%s\n' "$qvr_proc" | awk '{print $9}')
    sxr_cpu=$(printf '%s\n' "$sxr_proc" | awk '{print $9}')

    mem_total=$(printf '%s\n' "$MEM" | awk '/^MemTotal:/{print $2; exit}')
    mem_available=$(printf '%s\n' "$MEM" | awk '/^MemAvailable:/{print $2; exit}')
    mem_used=$(awk -v t="$mem_total" -v a="$mem_available" 'BEGIN { if (t != "" && a != "") print t-a }')

    charge_mAh=$(awk -v x="$charge" 'BEGIN { if (x != "") printf "%.3f", x/1000 }')
    current_mA=$(awk -v x="$current" 'BEGIN { if (x != "") printf "%.3f", x/1000 }')
    voltage_V=$(awk -v x="$voltage" 'BEGIN { if (x != "") printf "%.6f", x/1000000 }')
    battery_C=$(awk -v x="$btemp" 'BEGIN { if (x != "") printf "%.1f", x/10 }')
    skin_C=$(awk -v x="$skin" 'BEGIN { if (x != "") printf "%.2f", x }')
    cpu_max_C=$(awk -v x="$cpu_max" 'BEGIN { if (x != "") printf "%.2f", x }')
    cpu_avg_C=$(awk -v x="$cpu_avg" 'BEGIN { if (x != "") printf "%.2f", x }')

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$timestamp" "$elapsed" "$level" "$charge_mAh" "$current_mA" "$voltage_V" \
        "$status" "$usb" "$battery_C" "$skin_C" "$cpu_max_C" "$cpu_avg_C" \
        "$app_pid" "$app_cpu" "$app_res_kb" "$app_mem" "$mem_used" "$mem_available" "$system_cpu" \
        "$camera_cpu" "$qvr_cpu" "$sxr_cpu" \
        >>"$CSV_FILE"
    sleep "$INTERVAL"
done

ACTUAL_DURATION=$(($(date +%s) - SAMPLE_START))
ACTUAL_DURATION="$ACTUAL_DURATION" STAGE="$STAGE" awk -F, '
BEGIN { duration = ENVIRON["ACTUAL_DURATION"] }
NR == 1 { next }
{
    n++
    if ($14!="") { as+=$14; an++; if ($14>ax) ax=$14 }
    if ($15!="") { rs+=$15; rn++; if ($15>rx) rx=$15 }
    if ($19!="") { ss+=$19; sn++; if ($19>sx) sx=$19 }
    if ($9!="") { bs+=$9; bn++; if ($9>bx) bx=$9 }
    if ($10!="") { ks+=$10; kn++; if ($10>kx) kx=$10 }
    if ($11!="") { cs+=$11; cn++; if ($11>cx) cx=$11 }
    if ($17!="") { ms+=$17; mn++; if (mi==0 || $17<mi) mi=$17 }
    if ($3!="" && fl=="") fl=$3; if ($3!="") ll=$3
    if ($4!="" && fc=="") fc=$4; if ($4!="") lc=$4
}
END {
    print "stage=" ENVIRON["STAGE"]
    printf "samples=%d\nactual_duration_s=%s\n",n,duration
    if(an) printf "app_cpu_avg_pct=%.2f\napp_cpu_max_pct=%.2f\n",as/an,ax
    if(rn) printf "app_rss_avg_kb=%.0f\napp_rss_max_kb=%.0f\n",rs/rn,rx
    if(sn) printf "system_cpu_avg_pct=%.2f\nsystem_cpu_max_pct=%.2f\n",ss/sn,sx
    if(bn) printf "battery_temp_avg_C=%.2f\nbattery_temp_max_C=%.2f\n",bs/bn,bx
    if(kn) printf "skin_temp_avg_C=%.2f\nskin_temp_max_C=%.2f\n",ks/kn,kx
    if(cn) printf "cpu_temp_avg_C=%.2f\ncpu_temp_max_C=%.2f\n",cs/cn,cx
    if(mn) printf "system_mem_used_avg_kb=%.0f\nsystem_mem_used_min_kb=%.0f\n",ms/mn,mi
    if(fl!="" && ll!="") {
        d=fl-ll
        printf "battery_level_start_pct=%s\nbattery_level_end_pct=%s\nbattery_level_drop_pct_points=%s\n",fl,ll,d
        if(duration>0) printf "battery_level_drop_per_hour_pct_points=%.3f\n",d*3600/duration
    }
    if(fc!="" && lc!="") {
        d=fc-lc
        printf "charge_counter_start_mAh=%s\ncharge_counter_end_mAh=%s\ncharge_counter_drop_mAh=%s\n",fc,lc,d
        if(duration>0) printf "charge_counter_drop_per_hour_mAh=%.3f\n",d*3600/duration
    }
}
' "$CSV_FILE" | tee "$SUMMARY_FILE"

echo "completed: $SUMMARY_FILE"
