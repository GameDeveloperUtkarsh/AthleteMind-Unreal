"""Exercise the actual Unreal duel with native console controls and UDP pose frames."""

import json
import os
import math
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time
import uuid

from send_pose import send_pose


def main():
    root = Path(__file__).resolve().parents[1]
    cache_path = os.environ.get("ATHLETEMIND_TEST_DDC") or root / "DerivedDataCache"
    engine = Path(r"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe")
    (root / "Saved").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=root / "Saved") as tmp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        log = Path(tmp) / "duel.log"
        game = subprocess.Popen([
            str(engine), str(root / "MediapipeCombat.uproject"), "/Game/AthleteMind/Lvl_RehabDuel",
            "-NoSessionReport", "-game", "-nullrhi", "-nosound", "-unattended", "-NoCrashDialog", "-forcelogflush",
            "-DDC=NoZenLocalFallback", f"-LocalDataCachePath={cache_path}",
            f"-abslog={log}", f"-PoseBridgePort={port}", "-ExecCmds=DuelLive,DuelSide,t.MaxFPS 30,csvprofile start",
            "-csvExecCmds=30:DuelContinue,31:DuelGuard,r39:DuelContinue,r41:DuelCounter",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        session = str(uuid.uuid4())
        sequence = 0

        def log_text():
            return log.read_text(errors="replace") if log.exists() else ""

        def wait_for(message, count=1, timeout=12):
            until = time.monotonic() + timeout
            while time.monotonic() < until and game.poll() is None:
                if log_text().count(message) >= count:
                    return
                time.sleep(0.03)
            tail = [s for s in log_text().splitlines() if "Duel" in s or "Error:" in s]
            raise RuntimeError(f"Waiting for {message!r} failed.\n" + "\n".join(tail[-14:]))

        def sample(flexion, confidence, stamp):
            nonlocal sequence
            angle = math.radians(flexion)
            points = [[0, 0, 0, 0, 0] for _ in range(33)]
            points[24] = [0, -0.4, 0, confidence, confidence]
            points[26] = [0, 0, 0, confidence, confidence]
            points[28] = [0, 0.4 * math.cos(angle), 0.4 * math.sin(angle), confidence, confidence]
            send_pose(sender, port, session, sequence, stamp, points)
            sequence += 1

        def pose(flexion, seconds):
            until = time.monotonic() + seconds
            while time.monotonic() < until:
                sample(flexion, 1.0, time.monotonic_ns() // 1_000_000)
                time.sleep(0.035)

        try:
            wait_for("Camera setup opened", timeout=150)
            pose(26, 2)
            wait_for("Response selected: Guard")
            assert "CDO Constructor (DuelWarden)" not in log_text(), "The Warden could not load its assets"
            pose(26, 0.5)
            pose(60, 0.09)
            pose(0, 0.4)
            pose(60, 0.4)
            pose(105, 0.2)
            pose(0, 0.45)
            assert "Resolved rep" not in log_text(), "A brief hold or overshoot completed a turn"
            # Returning during an unobserved interval must discard the armed rep.
            pose(60, 0.5)
            time.sleep(.45)
            pose(26, .45)
            assert "Tracking gap: waiting for a fresh starting stance" in log_text()
            assert "Resolved rep" not in log_text(), "A return hidden by a tracking gap scored"
            pose(60, 0.15)
            sample(60, 0.1, time.monotonic_ns() // 1_000_000)
            pose(60, 0.15)
            pose(0, 0.35)
            assert "Resolved rep" not in log_text(), "A weak sample bridged two short target holds"
            pose(60, 0.42)
            # Return at the same depth after a camera blink; the gap cannot satisfy a hold.
            time.sleep(.43)
            pose(60, .20)
            sample(119, 1., time.monotonic_ns() // 1_000_000)
            pose(60, .20)
            assert "Resolved rep" not in log_text(), "A held target completed a turn"
            sample(60, 0.1, time.monotonic_ns() // 1_000_000)
            pose(26, 0.4)  # Return to the learned stance, above the default neutral limit.
            wait_for("Resolved rep 1: Relay strike")
            wait_for("Dodge cue ready")
            pose(65, 1.15)
            pose(0, 0.55)
            assert "Resolved dodge" not in log_text(), "A bent knee or short hold triggered a dodge"
            stamp = time.monotonic_ns() // 1_000_000
            sample(0, 0.1, stamp)
            wait_for("Tracking gap: waiting for a fresh starting stance", count=2)
            pose(0, 0.55)
            assert "Resolved dodge" not in log_text(), "An interrupted hold was carried across resume"
            pose(0, 0.75)
            wait_for("Resolved dodge 1")
            wait_for("Response selected: Counter")
            pose(0, 0.4)
            pose(65, 0.45)
            stamp = time.monotonic_ns() // 1_000_000
            sample(65, 0.1, stamp)
            wait_for("Tracking gap: waiting for a fresh starting stance", count=3)
            session = str(uuid.uuid4())
            # The bridge accepts a new session once its previous session is stale.
            pose(65, 1.1)
            pose(0, 0.45)
            assert log_text().count("Resolved rep") == 1, "Reacquisition at depth credited the partial rep"
            pose(60, 0.45)
            pose(0, 0.4)
            wait_for("Resolved rep 2: Charged attack")
            # The charged attack destroys the remaining armor before the five-turn cap.
            uid = re.search(r"Duel ready: ([A-Fa-f0-9-]+)", log_text()).group(1)
            summary = root / "Saved" / "RehabSessions" / (uid + ".json")
            until = time.monotonic() + 6
            while not summary.exists() and time.monotonic() < until:
                time.sleep(.05)
            data = json.loads(summary.read_text(encoding="utf-8-sig"))
            assert data["side"] == "right" and 28 < data["neutral_max_flexion_deg"] < 32, data
            assert data["completed"] == 2 and data["planned"] == 5 and data["interruptions"] == 3
            assert data["dodges"] == 1 and data["skipped_dodges"] == 0
            assert [r["response"] for r in data["reps"]] == ["relay_strike", "charged_attack"]
            assert data["finish_reason"] == "Relay restored"
            assert data["warden_hull_remaining"] == 0 and data["charge_remaining"] == 1
            assert all(59 <= r["estimated_peak_flexion_deg"] <= 63 for r in data["reps"])
            summary.unlink()  # Only the test's own verified output.
            print("PASS: two attacks, a steady-stance dodge, interrupted holds, reacquisition and victory before the set limit.")
        finally:
            if game.poll() is None:
                game.terminate()
            game.wait(timeout=15)


if __name__ == "__main__":
    main()
