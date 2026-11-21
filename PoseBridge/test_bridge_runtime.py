"""Run the synthetic integration test with the installed Windows UE 5.7 editor."""

import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import uuid

from send_pose import send_pose


def main():
    root = Path(__file__).resolve().parents[1]
    cache_path = os.environ.get("ATHLETEMIND_TEST_DDC") or root / "DerivedDataCache"
    engine = Path(r"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe")
    logs = root / "Saved" / "Logs"
    logs.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=logs) as tmp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        log = Path(tmp) / "bridge.log"
        game = subprocess.Popen([
            str(engine), str(root / "MediapipeCombat.uproject"), "-game", "-nullrhi",
            "-nosound", "-nosplash", "-unattended", "-NoCrashDialog", "-forcelogflush",
            "-DDC=NoZenLocalFallback", f"-LocalDataCachePath={cache_path}",
            f"-abslog={log}", f"-PoseBridgePort={port}",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)

        def wait_for(message, count=1, timeout=10):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline and game.poll() is None:
                if log.exists() and log.read_text(errors="replace").count(message) >= count:
                    return
                time.sleep(0.03)
            raise RuntimeError(f"Missing Unreal log message {message!r} (occurrence {count})")

        try:
            wait_for("Pose Waiting", timeout=150)
            address = ("127.0.0.1", port)
            for invalid in (b"", b"x" * 9000, b"{}", b"{"):
                sender.sendto(invalid, address)
            subprocess.run([sys.executable, str(root / "PoseBridge/send_pose.py"),
                            "--synthetic", "--port", str(port), "--frames", "40"], check=True)
            wait_for("Pose Tracked")
            wait_for("Pose Stale")

            session = str(uuid.uuid4())
            world = [[0, 0, 0, 1, 1] for _ in range(33)]
            for hip, ankle in ((23, 27), (24, 28)):
                world[hip][1] = -0.4
                world[ankle][1] = 0.4
            straight = "Knee L 180.0 deg  R 180.0 deg"
            previous = log.read_text(errors="replace").count(straight)
            send_pose(sender, port, session, 1, 1, world)
            wait_for(straight, count=previous + 1)
            send_pose(sender, port, session, 2, 2, [])
            wait_for("Pose NoPose")
            world[27][1:3] = [0, 0.4]
            world[28][3] = 0.1
            for seq, timestamp in ((1, 3), (3, 1), (2, 2)):
                send_pose(sender, port, session, seq, timestamp, world)
            invalid = dict(version=1, session=session, seq=999, capture_ms=999, tracked=True, world=[])
            sender.sendto(json.dumps(invalid).encode(), address)
            wait_for("Pose Stale", count=2)
            if "Knee L 90.0 deg" in log.read_text(errors="replace"):
                raise RuntimeError("An out-of-order or repeated frame replaced the no-pose state")
            send_pose(sender, port, session, 3, 3, world)
            wait_for("Knee L 90.0 deg  R --")
            print("PASS: native startup, sender restart, invalid packets, tracking loss, confidence and angles.")
        finally:
            if game.poll() is None:
                game.terminate()
            game.wait(timeout=15)


if __name__ == "__main__":
    main()
