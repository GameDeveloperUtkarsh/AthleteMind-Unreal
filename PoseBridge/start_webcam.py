"""Set up the Windows webcam sender and stream to the local Unreal game."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from urllib.request import urlopen
import zipfile


BRIDGE_DIR = Path(__file__).resolve().parent
MODEL_URL = ("https://storage.googleapis.com/mediapipe-models/pose_landmarker/"
             "pose_landmarker_lite/float16/1/pose_landmarker_lite.task")


def valid_model(path):
    if not path.is_file() or not zipfile.is_zipfile(path):
        return False
    try:
        with zipfile.ZipFile(path) as bundle:
            return sum(name.endswith(".tflite") for name in bundle.namelist()) >= 2 and bundle.testzip() is None
    except zipfile.BadZipFile:
        return False


def prepare_model(path):
    if valid_model(path):
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    print("Downloading Google's Pose Landmarker Lite model...", flush=True)
    with tempfile.TemporaryDirectory(dir=path.parent) as staging:
        download = Path(staging) / path.name
        with urlopen(MODEL_URL, timeout=45) as response, download.open("wb") as output:
            shutil.copyfileobj(response, output)
        if not valid_model(download):
            raise ValueError("The downloaded file is not a valid Pose Landmarker model bundle.")
        download.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera", type=int, default=0, help="Webcam index (default: 0).")
    parser.add_argument("--port", type=int, default=8765, help="Unreal receiver port (default: 8765).")
    parser.add_argument("--no-preview", action="store_true", help="Run without the webcam window.")
    args = parser.parse_args()
    if args.camera < 0 or not 1 <= args.port <= 65535:
        parser.error("expected a nonnegative camera index and a port between 1 and 65535")

    env = BRIDGE_DIR / ".venv"
    python = env / "Scripts" / "python.exe"
    model = BRIDGE_DIR / "models" / "pose_landmarker_lite.task"
    try:
        if not python.exists():
            subprocess.run([sys.executable, "-m", "venv", str(env)], check=True)
        subprocess.run([str(python), "-m", "pip", "--disable-pip-version-check", "install",
                        "-r", str(BRIDGE_DIR / "requirements.txt")], check=True)
        prepare_model(model)
        command = [str(python), str(BRIDGE_DIR / "send_pose.py"), "--model", str(model),
                   "--camera", str(args.camera), "--port", str(args.port)]
        if not args.no_preview:
            command.append("--preview")
        return subprocess.run(command).returncode
    except KeyboardInterrupt:
        return 0
    except subprocess.CalledProcessError as error:
        print("Webcam environment setup failed. See the installer output above.", file=sys.stderr)
        return error.returncode
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"Webcam setup failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
