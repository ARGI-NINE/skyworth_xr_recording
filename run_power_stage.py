"""Run one SXR power stage and keep the SDK control connection alive."""

import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import time


def vi(value: int) -> bytes:
    out = bytearray()
    while value >= 128:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def field_varint(number: int, value: int) -> bytes:
    return vi((number << 3) | 0) + vi(value)


def field_message(number: int, payload: bytes) -> bytes:
    return vi((number << 3) | 2) + vi(len(payload)) + payload


def packet(sequence: int, command: int) -> bytes:
    command_payload = field_varint(1, command)
    payload = (
        field_varint(1, sequence)
        + field_varint(2, int(time.time() * 1000))
        + field_varint(3, 1)
        + field_message(10, command_payload)
    )
    return b"EG" + struct.pack(">I", len(payload)) + payload


def read_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("SDK control connection closed")
        data.extend(chunk)
    return bytes(data)


def read_varint(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    shift = 0
    while offset < len(data):
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7
        if shift > 63:
            raise ValueError("protobuf varint is too long")
    raise ValueError("truncated protobuf varint")


def protobuf_fields(data: bytes):
    offset = 0
    while offset < len(data):
        key, offset = read_varint(data, offset)
        number = key >> 3
        wire_type = key & 7
        if wire_type == 0:
            value, offset = read_varint(data, offset)
        elif wire_type == 1:
            value = data[offset:offset + 8]
            offset += 8
        elif wire_type == 2:
            size, offset = read_varint(data, offset)
            value = data[offset:offset + size]
            offset += size
        elif wire_type == 5:
            value = data[offset:offset + 4]
            offset += 4
        else:
            raise ValueError(f"unsupported protobuf wire type: {wire_type}")
        yield number, wire_type, value


def parse_device_state(payload: bytes):
    """Extract DeviceState from the SDK status packet for independent proof of mode."""
    for number, wire_type, status in protobuf_fields(payload):
        if number != 20 or wire_type != 2:
            continue
        for status_number, status_wire, device in protobuf_fields(status):
            if status_number != 3 or status_wire != 2:
                continue
            values = {}
            for field_number, field_wire, value in protobuf_fields(device):
                if field_wire == 0 and field_number in (2, 3, 4):
                    values[field_number] = value
            if 2 in values:
                return {
                    "operation_mode": values.get(2),
                    "operation_phase": values.get(3),
                    "revision": values.get(4),
                }
    return None


def record_state(state, state_log: str) -> None:
    state = dict(state)
    state["host_time_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    with open(state_log, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(state, sort_keys=True) + "\n")


def drain(sock: socket.socket, seconds: float, state_log: str):
    latest = None
    deadline = time.time() + seconds
    sock.settimeout(0.25)
    while time.time() < deadline:
        try:
            header = read_exact(sock, 6)
            if header[:2] != b"EG":
                raise ValueError("invalid EG frame magic")
            length = struct.unpack(">I", header[2:])[0]
            if length > 16 * 1024 * 1024:
                raise ValueError("invalid EG frame length")
            payload = read_exact(sock, length)
            state = parse_device_state(payload)
            if state is not None:
                latest = state
                record_state(state, state_log)
        except socket.timeout:
            pass
    return latest


def send(sock: socket.socket, sequence: int, command: int, wait: float,
         state_log: str) -> int:
    print(f"control command={command}", flush=True)
    sock.sendall(packet(sequence, command))
    drain(sock, wait, state_log)
    return sequence + 1


def wait_for_mode(sock: socket.socket, mode: int, timeout: float, state_log: str) -> None:
    deadline = time.time() + timeout
    latest = None
    while time.time() < deadline:
        state = drain(sock, 0.5, state_log)
        if state is not None:
            latest = state
            if state.get("operation_mode") == mode:
                print(
                    "verified operation_mode={} operation_phase={} revision={}".format(
                        state.get("operation_mode"),
                        state.get("operation_phase"),
                        state.get("revision"),
                    ),
                    flush=True,
                )
                return
    raise RuntimeError(
        f"state verification timeout: expected operation_mode={mode}, last={latest}"
    )


def connect(port: int) -> socket.socket:
    last = None
    for _ in range(20):
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=5)
        except OSError as exc:
            last = exc
            time.sleep(0.5)
    raise last


def adb_command(adb: str, *args: str) -> None:
    subprocess.run([adb, *args], check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", required=True,
                        choices=("idle", "preview", "phone_record", "local_record"))
    parser.add_argument("--duration", type=int, default=300)
    parser.add_argument("--interval", default="2")
    parser.add_argument("--label", default="")
    parser.add_argument("--adb", default=r"C:\adb\adb.exe")
    parser.add_argument("--bash", default=r"D:\Git\bin\bash.exe")
    parser.add_argument("--port", type=int, default=18801)
    args = parser.parse_args()

    root = os.path.dirname(os.path.abspath(__file__))
    collector = os.path.join(root, "get_power.sh")
    label = args.label or args.stage
    forward = False
    sock = None
    process = None
    stage_started = False
    stage_stopped = False
    trace_dir = os.path.join(root, "power_tests", "state_traces")
    os.makedirs(trace_dir, exist_ok=True)
    trace_name = "{}-{}.jsonl".format(
        time.strftime("%Y%m%d_%H%M%S", time.gmtime()), label
    )
    state_log = os.path.join(trace_dir, trace_name)
    print(f"state_trace={state_log}", flush=True)

    try:
        adb_command(args.adb, "forward", f"tcp:{args.port}", "tcp:8801")
        forward = True
        sock = connect(args.port)
        sequence = 1

        # Normalize all prior state.  These commands are idempotent.
        sequence = send(sock, sequence, 2, 8, state_log)
        sequence = send(sock, sequence, 4, 4, state_log)
        wait_for_mode(sock, 0, 10, state_log)
        time.sleep(2)

        if args.stage == "idle":
            pass
        elif args.stage == "local_record":
            sock.close()
            sock = None
            adb_command(args.adb, "shell", "am", "broadcast",
                        "-a", "com.ssnwt.helloxr.START_RECORDING")
            sock = connect(args.port)
            wait_for_mode(sock, 2, 15, state_log)
            stage_started = True
        elif args.stage == "preview":
            sequence = send(sock, sequence, 3, 5, state_log)
            wait_for_mode(sock, 1, 15, state_log)
            stage_started = True
        elif args.stage == "phone_record":
            sequence = send(sock, sequence, 3, 5, state_log)
            wait_for_mode(sock, 1, 15, state_log)
            sequence = send(sock, sequence, 1, 10, state_log)
            wait_for_mode(sock, 4, 15, state_log)
            stage_started = True

        process = subprocess.Popen([
            args.bash, collector,
            "--stage", args.stage,
            "--duration", str(args.duration),
            "--interval", str(args.interval),
            "--label", label,
            "--adb", args.adb,
        ])

        # Keep draining the control socket while the collector runs.  Closing
        # it during phone preview/record would be interpreted as a disconnect.
        while process.poll() is None:
            if sock is not None:
                try:
                    drain(sock, 0.25, state_log)
                except (ConnectionError, OSError):
                    raise RuntimeError("SDK control connection dropped during stage")
            else:
                time.sleep(0.25)

        if args.stage == "local_record":
            adb_command(args.adb, "shell", "am", "broadcast",
                        "-a", "com.ssnwt.helloxr.STOP_RECORDING")
            time.sleep(8)
            stage_stopped = True
            if sock is not None:
                wait_for_mode(sock, 0, 15, state_log)
        elif args.stage == "preview" and sock is not None:
            send(sock, sequence, 4, 6, state_log)
            sequence += 1
            wait_for_mode(sock, 0, 15, state_log)
            stage_stopped = True
        elif args.stage == "phone_record" and sock is not None:
            send(sock, sequence, 2, 12, state_log)
            sequence += 1
            wait_for_mode(sock, 0, 15, state_log)
            stage_stopped = True
        return process.returncode or 0
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            process.wait(timeout=15)
        if stage_started and not stage_stopped:
            try:
                if args.stage == "local_record":
                    adb_command(args.adb, "shell", "am", "broadcast",
                                "-a", "com.ssnwt.helloxr.STOP_RECORDING")
                elif sock is not None:
                    send(sock, 999999, 4 if args.stage == "preview" else 2,
                         2, state_log)
            except (OSError, subprocess.CalledProcessError, RuntimeError):
                pass
        if sock is not None:
            sock.close()
        if forward:
            subprocess.run([args.adb, "forward", "--remove", f"tcp:{args.port}"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    sys.exit(main())
