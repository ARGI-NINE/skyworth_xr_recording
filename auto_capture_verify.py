#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import subprocess
import time
import sys


ADB = "adb"
ADB_DEVICE = "192.168.2.106:5555"

PACKAGE = "com.ssnwt.helloxr"
ACTIVITY = "com.ssnwt.helloxr.VrNativeActivity"

BASE_DIR = "/storage/emulated/0/Android/data/com.ssnwt.helloxr/files"
DATASET_ROOT = BASE_DIR + "/dataset"

START_ACTION = "com.ssnwt.helloxr.START_RECORDING"
STOP_ACTION = "com.ssnwt.helloxr.STOP_RECORDING"

CYCLES = 200
RECORD_SECONDS = 120

AFTER_STOP_WAIT_SECONDS = 5
VERIFY_TIMEOUT_SECONDS = 60

REQUIRED_FILES = [
    "accel.csv",
    "audio.m4a",
    "camera_params_ctrl.json",
    "camera_params_rgb.json",
    "camera_params_tracking.json",
    "capture_status.json",
    "ctrl.mp4",
    "gyro.csv",
    "hand_tracking.csv",
    "head_pose.csv",
    "rgb.mp4",
    "time_offset.json",
    "tracking.mp4",
    "capture.log",
]


def run_cmd(cmd, timeout=60):
    return subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )


def adb_cmd(*args):
    return [ADB, "-s", ADB_DEVICE, *args]


def adb_shell(cmd, timeout=60):
    return run_cmd(adb_cmd("shell", cmd), timeout=timeout)


def q(path: str) -> str:
    return "'" + path.replace("'", "'\"'\"'") + "'"


def list_dataset_dirs():
    cmd = f'for d in {q(DATASET_ROOT)}/*; do [ -d "$d" ] && echo "$d"; done'
    p = adb_shell(cmd, timeout=30)

    dirs = []
    for line in p.stdout.splitlines():
        line = line.strip()
        if line:
            dirs.append(line)

    return sorted(dirs)


def read_remote_file(path):
    p = adb_shell(f"cat {q(path)} 2>/dev/null", timeout=30)
    if p.returncode != 0:
        return None
    return p.stdout


def remote_file_size(path):
    cmd = f"if [ -f {q(path)} ]; then wc -c < {q(path)}; else echo -1; fi"
    p = adb_shell(cmd, timeout=30)

    try:
        return int(p.stdout.strip().splitlines()[-1])
    except Exception:
        return -1


def find_new_dataset_dir(before_dirs):
    before_set = set(before_dirs)

    for _ in range(30):
        now_dirs = list_dataset_dirs()
        new_dirs = sorted(set(now_dirs) - before_set)

        if new_dirs:
            return new_dirs[-1]

        time.sleep(1)

    return None


def verify_dataset(dataset_dir):
    reasons = []

    # 1. 检查必须文件存在且非 0 字节
    for name in REQUIRED_FILES:
        path = dataset_dir + "/" + name
        size = remote_file_size(path)

        if size < 0:
            reasons.append(f"missing file: {name}")
        elif size == 0:
            reasons.append(f"zero-size file: {name}")

    # 2. 检查 capture_status.json
    status_path = dataset_dir + "/capture_status.json"
    status_content = read_remote_file(status_path)

    if status_content is None:
        reasons.append("cannot read capture_status.json")
    else:
        if '"state": "complete"' not in status_content:
            reasons.append('capture_status.json does not contain: "state": "complete"')

    # 3. 检查 capture.log
    log_path = dataset_dir + "/capture.log"
    log_content = read_remote_file(log_path)

    if log_content is None:
        reasons.append("cannot read capture.log")
    else:
        if "[error]" in log_content.lower():
            reasons.append("capture.log contains [error]")

    return len(reasons) == 0, reasons


def wait_verify_dataset(dataset_dir):
    deadline = time.time() + VERIFY_TIMEOUT_SECONDS
    last_reasons = ["verify not started"]

    while time.time() < deadline:
        ok, reasons = verify_dataset(dataset_dir)

        if ok:
            return True, []

        last_reasons = reasons

        if any("[error]" in r for r in reasons):
            return False, reasons

        time.sleep(2)

    return False, last_reasons


def start_app():
    print("[INIT] wait for device")
    p = run_cmd(adb_cmd("wait-for-device"), timeout=60)
    if p.returncode != 0:
        print("[INIT] adb wait-for-device failed")
        print(p.stderr)
        sys.exit(1)

    print("[INIT] start app")
    p = run_cmd(
        adb_cmd(
            "shell",
            "am",
            "start",
            "-n",
            f"{PACKAGE}/{ACTIVITY}",
        ),
        timeout=30,
    )

    if p.returncode != 0:
        print("[INIT] start app failed")
        print(p.stderr)
        sys.exit(1)

    time.sleep(5)


def main():
    start_app()

    pass_count = 0
    fail_count = 0
    failed_cycles = []

    for cycle in range(1, CYCLES + 1):
        print(f"\n========== cycle {cycle}/{CYCLES} ==========")

        before_dirs = list_dataset_dirs()

        print(f"[{cycle}] start recording")
        p = run_cmd(
            adb_cmd(
                "shell",
                "am",
                "broadcast",
                "-a",
                START_ACTION,
            ),
            timeout=30,
        )

        if p.returncode != 0:
            fail_count += 1
            failed_cycles.append(cycle)
            print(f"[{cycle}] FAIL: START_RECORDING broadcast failed")
            print(p.stderr.strip())
            continue

        time.sleep(RECORD_SECONDS)

        print(f"[{cycle}] stop recording")
        p = run_cmd(
            adb_cmd(
                "shell",
                "am",
                "broadcast",
                "-a",
                STOP_ACTION,
            ),
            timeout=30,
        )

        if p.returncode != 0:
            fail_count += 1
            failed_cycles.append(cycle)
            print(f"[{cycle}] FAIL: STOP_RECORDING broadcast failed")
            print(p.stderr.strip())
            continue

        time.sleep(AFTER_STOP_WAIT_SECONDS)

        dataset_dir = find_new_dataset_dir(before_dirs)

        if not dataset_dir:
            fail_count += 1
            failed_cycles.append(cycle)
            print(f"[{cycle}] FAIL: no new dataset dir found")
            continue

        print(f"[{cycle}] dataset: {dataset_dir}")

        ok, reasons = wait_verify_dataset(dataset_dir)

        if ok:
            pass_count += 1
            print(f"[{cycle}] PASS")
        else:
            fail_count += 1
            failed_cycles.append(cycle)
            print(f"[{cycle}] FAIL")
            for reason in reasons:
                print(f"  - {reason}")

    print("\n========== final result ==========")
    print(f"total: {CYCLES}")
    print(f"pass : {pass_count}")
    print(f"fail : {fail_count}")

    if fail_count == 0:
        print("ALL PASS: 200 cycles all completed correctly")
        sys.exit(0)
    else:
        print(f"FAILED cycles: {failed_cycles}")
        sys.exit(2)


if __name__ == "__main__":
    main()
