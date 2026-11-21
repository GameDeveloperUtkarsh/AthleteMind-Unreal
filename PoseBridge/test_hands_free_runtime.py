"""Run the Unreal encounter through its gamepad bindings and hands-free pose receiver."""
import json
import os
import math
import re
import socket
import subprocess
import tempfile
import time
import uuid
from pathlib import Path

from send_pose import send_pose


def main():
    root = Path(__file__).resolve().parents[1]
    cache_path = os.environ.get("ATHLETEMIND_TEST_DDC") or root / "DerivedDataCache"
    engine = Path(r"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe")
    session = str(uuid.uuid4())
    seq = 0
    (root / "Saved").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=root / "Saved") as folder, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserve:
            reserve.bind(("127.0.0.1", 0))
            port = reserve.getsockname()[1]
        log = Path(folder) / "duel.log"
        game = subprocess.Popen([
            str(engine), str(root / "MediapipeCombat.uproject"), "-NoSessionReport", "-game", "-nullrhi", "-nosound",
            "-unattended", "-NoCrashDialog", "-forcelogflush", f"-PoseBridgePort={port}", f"-abslog={log}",
            "-DDC=NoZenLocalFallback", f"-LocalDataCachePath={cache_path}",
            # Exercise the prescribed set limit with a three-turn test profile.
            "-ini:Game:[/Script/MediapipeCombat.RehabDuelController]:Profile=(PlannedReps=3)",
            "-ExecCmds=t.MaxFPS 30,csvprofile start",
            "-csvExecCmds=15:Input.+key Gamepad_DPad_Right,16:Input.-key Gamepad_DPad_Right,"
            "28:Input.+key Gamepad_FaceButton_Bottom,29:Input.-key Gamepad_FaceButton_Bottom",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)

        def logs():
            return log.read_text(errors="replace") if log.exists() else ""

        def wait_for(text, timeout=8):
            until = time.monotonic() + timeout
            while game.poll() is None and time.monotonic() < until:
                if text in logs():
                    return
                time.sleep(.05)
            raise AssertionError(f"Missing {text}. " + "\n".join(logs().splitlines()[-15:]))

        def sample(seconds, gesture="down", curl=0, lift=0, confidence=1, other_hand="down", knee=0, hide_arm=False):
            nonlocal seq
            until = time.monotonic() + seconds
            while time.monotonic() < until:
                points = [[0, 0, 0, 0, 0] for _ in range(33)]
                for side in (0, 1):
                    x = side * .3
                    shoulder = [x, -.9, 0, 1, 1]
                    theta = math.radians(lift if side == 0 else 0)
                    phi = theta + math.radians(curl if side == 0 else 0)
                    elbow = [x, shoulder[1]+.28*math.cos(theta), .28*math.sin(theta), 1, 1]
                    wrist = [x, elbow[1]+.28*math.cos(phi), elbow[2]+.28*math.sin(phi), 1, 1]
                    if (side == 0 and gesture == "select") or (side == 1 and other_hand == "select"):
                        wrist[:3] = [x, -.82, .07]
                    if (side == 0 and gesture == "next") or (side == 1 and other_hand == "reach"):
                        wrist[:3] = [x, -.86, .48]
                    if side == 0:
                        wrist[3:] = [confidence, confidence]
                    points[11+side], points[13+side], points[15+side] = shoulder, elbow, wrist
                    if side == 0 and hide_arm:
                        for j in (11, 13, 15):
                            points[j][3:] = [0, 0]
                    for joint, y in ((23, -.4), (25, 0), (27, .4)):
                        points[joint+side] = [x, y, 0, 1, 1]
                angle = math.radians(knee)
                points[27][:3] = [0, .4 * math.cos(angle), .4 * math.sin(angle)]
                send_pose(sock, port, session, seq, time.monotonic_ns() // 1_000_000, points)
                seq += 1
                time.sleep(.033)

        def command(gesture):
            sample(.30)
            sample(1.05, gesture)

        try:
            wait_for("Camera setup opened", timeout=150)
            sample(.35)
            sample(.5, "select")
            sample(.13, "select", confidence=.05)
            sample(.7, "select")
            assert "Duel ready:" not in logs(), "Weak or partial gesture selected a mode"
            command("select")
            wait_for("Duel ready:")
            sample(1.05, "select")
            assert "Response selected:" not in logs(), "Selection repeated across screen transition"
            command("next")
            sample(1.0, "next")  # Holding one reach must only step once.
            command("next")
            command("select")
            wait_for("Response selected: Ward")
            sample(.7, "select")
            sample(.35)
            assert "Resolved rep" not in logs(), "Menu selection was scored as an exercise"
            sample(1.4, other_hand="select")
            assert "Hands-free pause" not in logs(), "A folded arm paused the exercise"
            sample(.50, other_hand="reach")
            sample(1.55, other_hand="select")
            wait_for("Hands-free pause")
            command("select")
            wait_for("Continued encounter")
            sample(.45)
            # Curling with the elbow raised does not match the card.
            sample(.55, curl=85, lift=55)
            sample(.48)
            assert "Resolved rep" not in logs()
            sample(.40, curl=85)
            sample(.03, curl=140)  # One noisy elbow estimate during return.
            sample(.18, curl=85)
            sample(.42)
            wait_for("Resolved rep 1: Pulse ward cast")
            sample(1.95)
            command("select")  # Ward cancels the next enemy pulse.
            assert "Dodge cue ready" not in logs()
            for _ in range(3):
                command("next")
            command("select")
            wait_for("Response selected: Flare")
            # A lift uses hip, shoulder and elbow. The wrist can leave the frame.
            sample(.5, confidence=0)
            sample(.5, lift=-52, confidence=0)
            sample(.45, confidence=0)
            assert "Resolved rep 2" not in logs(), "A backward lift was credited"
            sample(.4, lift=52, confidence=0)
            sample(.03, lift=110, confidence=0)
            sample(.18, lift=52, confidence=0)
            sample(.45, confidence=0)
            wait_for("Resolved rep 2: Signal flare landed")
            sample(1.95)
            command("select")
            wait_for("Dodge cue ready")
            sample(.20)
            sample(.48, other_hand="reach")
            sample(1.5, other_hand="select")
            assert logs().count("Hands-free pause") == 2 and "Resolved dodge" not in logs()
            command("select")
            sample(1.55)
            wait_for("Resolved dodge 1")
            sample(1.95)
            command("select")  # Ready to attack.
            # After a stream gap, arriving already bent cannot select Relay Strike.
            time.sleep(.45)
            sample(.65, knee=62, hide_arm=True)
            assert "Squat selected Relay strike" not in logs()
            sample(.45, hide_arm=True)
            sample(1.55, knee=62, hide_arm=True, other_hand="select")
            wait_for("Squat selected Relay strike")
            sample(.40, hide_arm=True, other_hand="select")
            wait_for("Resolved rep 3: Relay strike")
            assert logs().count("Hands-free pause") == 2
            sample(2.0)
            uid = re.search(r"Duel ready: ([A-Fa-f0-9-]+)", logs()).group(1)
            summary = root / "Saved" / "RehabSessions" / (uid + ".json")
            data = json.loads(summary.read_text(encoding="utf-8-sig"))
            assert data["hands_free_menus"] and data["completed"] == data["planned"] == 3
            assert (data["wards_cast"], data["dodges"], data["charge_remaining"]) == (1, 1, 4)
            curl, lift, squat = data["reps"]
            assert squat["exercise"] == "squat"
            assert curl["exercise"] == "elbow_curl" and abs(curl["estimated_peak_flexion_deg"]-85) < 1
            assert lift["exercise"] == "arm_lift" and abs(lift["estimated_peak_arm_elevation_deg"]-52) < 1
            assert data["finish_reason"] == "Set limit reached"
            summary.unlink()
            print("PASS: gamepad input, confidence/release gates, hands-free pause and resume, curl ward, arm lift, dodge and finish.")
        finally:
            if game.poll() is None:
                game.terminate()
            game.wait(timeout=15)


if __name__ == "__main__":
    main()
