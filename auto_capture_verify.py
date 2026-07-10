#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import subprocess
import time
import sys
import json


ADB = "adb"
ADB_DEVICE = "192.168.2.63:5555"

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
    "audio_metainfo.csv",
    "camera_params_ctrl.json",
    "camera_params_rgb.json",
    "camera_params_tracking.json",
    "capture_status.json",
    "capture.log",
    "ctrl.mp4",
    "ctrl_metainfo.csv",
    "gyro.csv",
    "head_pose.csv",
    "imu_calibration.json",
    "rgb.mp4",
    "rgb_metainfo.csv",
    "tracking.mp4",
    "tracking_metainfo.csv",
]

ONE_OF_FILE_GROUPS = [
    ("hand_tracking.csv", "controller_poses.csv"),
]

OPTIONAL_FILES = []

CSV_FILES_REQUIRING_DATA = [
    "accel.csv",
    "audio_metainfo.csv",
    "ctrl_metainfo.csv",
    "gyro.csv",
    "head_pose.csv",
    "rgb_metainfo.csv",
    "tracking_metainfo.csv",
]

HEADER_PREFIX_CHECKS = {
    "accel.csv": "timestamp_ns,x,y,z",
    "audio_metainfo.csv": "packet_index,pts_us,capture_utc_ns",
    "controller_poses.csv": "frame_number,timestamp_ns,left_active,left_px,left_py,left_pz,left_qx,left_qy,left_qz,left_qw,right_active,right_px,right_py,right_pz,right_qx,right_qy,right_qz,right_qw",
    "ctrl_metainfo.csv": "frame_index,frame_id,pts_us,exposure_start_utc_ns,exposure_duration_ns,gain,mid_exposure_utc_ns",
    "gyro.csv": "timestamp_ns,x,y,z",
    "hand_tracking.csv": "frame_number,timestamp,left_active,right_active",
    "head_pose.csv": "timestamp_ns,pos_x,pos_y,pos_z,quat_x,quat_y,quat_z,quat_w",
    "rgb_metainfo.csv": "frame_index,frame_id,pts_us,exposure_start_utc_ns,exposure_duration_ns,gain,mid_exposure_utc_ns",
    "tracking_metainfo.csv": "frame_index,frame_id,pts_us,exposure_start_utc_ns,exposure_duration_ns,gain,mid_exposure_utc_ns",
}


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


def read_remote_json(path):
    content = read_remote_file(path)

    if content is None:
        return None, "cannot read"

    try:
        return json.loads(content), None
    except json.JSONDecodeError as exc:
        return None, f"invalid json: {exc}"


def remote_text_line_count(path):
    p = adb_shell(f"if [ -f {q(path)} ]; then wc -l < {q(path)}; else echo -1; fi", timeout=30)
    try:
        return int(p.stdout.strip().splitlines()[-1])
    except Exception:
        return -1


def verify_csv_header(path, expected_prefix):
    p = adb_shell(f"if [ -f {q(path)} ]; then head -n 1 {q(path)}; fi", timeout=30)
    if p.returncode != 0:
        return False, "cannot read file"

    first_line = p.stdout.splitlines()[0].strip() if p.stdout.splitlines() else ""
    if not first_line.startswith(expected_prefix):
        return False, f"unexpected header: {first_line}"

    return True, None


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
    warnings = []

    # 1. 检查必须文件存在且非 0 字节
    for name in REQUIRED_FILES:
        path = dataset_dir + "/" + name
        size = remote_file_size(path)

        if size < 0:
            reasons.append(f"missing file: {name}")
        elif size == 0:
            reasons.append(f"zero-size file: {name}")

    # 2. 检查二选一文件
    for group in ONE_OF_FILE_GROUPS:
        existing = []
        for name in group:
            if remote_file_size(dataset_dir + "/" + name) > 0:
                existing.append(name)

        if not existing:
            reasons.append("missing one-of files: " + " / ".join(group))

    # 3. 可选文件：存在则要求非空
    for name in OPTIONAL_FILES:
        size = remote_file_size(dataset_dir + "/" + name)
        if size == 0:
            reasons.append(f"zero-size optional file: {name}")
        elif size < 0:
            warnings.append(f"optional file not found: {name}")

    # 4. 检查关键 CSV 至少有表头 + 1 行数据
    csvs_requiring_data = list(CSV_FILES_REQUIRING_DATA)
    for group in ONE_OF_FILE_GROUPS:
        for name in group:
            if remote_file_size(dataset_dir + "/" + name) > 0:
                csvs_requiring_data.append(name)

    for name in csvs_requiring_data:
        line_count = remote_text_line_count(dataset_dir + "/" + name)
        if line_count < 0:
            reasons.append(f"cannot read csv: {name}")
        elif line_count < 2:
            reasons.append(f"csv has no data rows: {name}")

    # 5. 检查关键 CSV 表头
    for name, expected_header in HEADER_PREFIX_CHECKS.items():
        if remote_file_size(dataset_dir + "/" + name) <= 0:
            continue
        ok, detail = verify_csv_header(dataset_dir + "/" + name, expected_header)
        if not ok:
            reasons.append(f"{name} header check failed: {detail}")

    # 6. 检查 capture_status.json
    status_path = dataset_dir + "/capture_status.json"
    status_json, status_error = read_remote_json(status_path)

    if status_error:
        reasons.append(f"capture_status.json {status_error}")
    else:
        if status_json.get("state") != "complete":
            reasons.append('capture_status.json state is not "complete"')
        if status_json.get("dataset_dir") != dataset_dir:
            reasons.append("capture_status.json dataset_dir mismatch")
        if status_json.get("capture_duration_ms", 0) <= 0:
            reasons.append("capture_status.json capture_duration_ms <= 0")

    # 7. 检查相机参数 JSON 可解析
    for name in [
        "camera_params_ctrl.json",
        "camera_params_rgb.json",
        "camera_params_tracking.json",
    ]:
        _, error = read_remote_json(dataset_dir + "/" + name)
        if error:
            reasons.append(f"{name} {error}")

    # 8. IMU 标定文件是条件性产物：存在时要求 JSON 合法
    imu_path = dataset_dir + "/imu_calibration.json"
    if remote_file_size(imu_path) > 0:
        _, error = read_remote_json(imu_path)
        if error:
            reasons.append(f"imu_calibration.json {error}")

    # 9. 检查 capture.log
    log_content = read_remote_file(dataset_dir + "/capture.log")
    if log_content is None:
        reasons.append("cannot read capture.log")
    elif "[error]" in log_content.lower():
        reasons.append("capture.log contains [error]")

    return len(reasons) == 0, reasons, warnings


def wait_verify_dataset(dataset_dir):
    deadline = time.time() + VERIFY_TIMEOUT_SECONDS
    last_reasons = ["verify not started"]
    last_warnings = []

    while time.time() < deadline:
        ok, reasons, warnings = verify_dataset(dataset_dir)

        if ok:
            return True, [], warnings

        last_reasons = reasons
        last_warnings = warnings

        time.sleep(2)

    return False, last_reasons, last_warnings


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

        ok, reasons, warnings = wait_verify_dataset(dataset_dir)

        if ok:
            pass_count += 1
            print(f"[{cycle}] PASS")
            for warning in warnings:
                print(f"  - WARN: {warning}")
        else:
            fail_count += 1
            failed_cycles.append(cycle)
            print(f"[{cycle}] FAIL")
            for reason in reasons:
                print(f"  - {reason}")
            for warning in warnings:
                print(f"  - WARN: {warning}")

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
