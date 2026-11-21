"""Stream one MediaPipe pose at a time to the local Unreal receiver."""

import argparse
import itertools
import json
import math
from pathlib import Path
import socket
import sys
import time
import uuid


LANDMARK_COUNT = 33


def send_pose(sock, port, session, sequence, capture_ms, landmarks):
    packet = {
        "version": 1,
        "session": session,
        "seq": sequence,
        "capture_ms": capture_ms,
        "tracked": bool(landmarks),
        "world": landmarks,
    }
    payload = json.dumps(packet, separators=(",", ":"), allow_nan=False).encode("utf-8")
    sock.sendto(payload, ("127.0.0.1", port))


def synthetic_frames(fps):
    """Generate two knee triplets with a known included angle of 90 to 180 degrees."""
    for index in itertools.count():
        capture_ms = time.monotonic_ns() // 1_000_000
        bend = math.pi * (1.0 - math.cos(2.0 * math.pi * index / (4 * fps))) / 4.0
        world = [(0.0, 0.0, 0.0, 0.0, 0.0)] * LANDMARK_COUNT
        for hip, knee, ankle, x in ((23, 25, 27, -0.15), (24, 26, 28, 0.15)):
            world[hip] = (x, 0.0, 0.0, 1.0, 1.0)
            world[knee] = (x, 0.4, 0.0, 1.0, 1.0)
            world[ankle] = (x, 0.4 + 0.4 * math.cos(bend), 0.4 * math.sin(bend), 1.0, 1.0)
        yield capture_ms, [tuple(round(value, 5) for value in point) for point in world]
        time.sleep(1.0 / fps)


def encode_world_landmarks(poses, image_poses=None):
    if not poses or len(poses[0]) != LANDMARK_COUNT:
        return []

    if image_poses is not None and (not image_poses or len(image_poses[0]) != LANDMARK_COUNT):
        return []

    world = []
    for index, landmark in enumerate(poses[0]):
        position = (landmark.x, landmark.y, landmark.z)
        confidence = (landmark.visibility or 0.0, landmark.presence or 0.0)
        if not all(value is not None and -10.0 <= value <= 10.0 for value in position):
            world.append((0, 0, 0, 0, 0))
            continue
        if not all(0.0 <= value <= 1.0 for value in confidence):
            confidence = (0.0, 0.0)
        if image_poses is not None:
            pixel = image_poses[0][index]
            if any(v is None or not 0 <= v <= 1 for v in (pixel.x, pixel.y)):
                confidence = (0.0, 0.0)
        world.append(tuple(round(value, 5) for value in (*position, *confidence)))
    return world


def show_camera_preview(cv2, frame, poses, world):
    height, width = frame.shape[:2]
    points = {}
    if poses and len(poses[0]) == LANDMARK_COUNT:
        for index in (11, 12, 13, 14, 15, 16, 23, 24, 25, 26, 27, 28):
            p = poses[0][index]
            if (p.x is not None and p.y is not None and 0 <= p.x <= 1 and 0 <= p.y <= 1
                    and world and min(world[index][3:]) >= 0.5):
                points[index] = (round(p.x * (width - 1)), round(p.y * (height - 1)))
    for a, b in ((11, 13), (13, 15), (12, 14), (14, 16), (11, 12), (11, 23), (12, 24), (23, 24), (23, 25), (25, 27), (24, 26), (26, 28)):
        if a in points and b in points:
            cv2.line(frame, points[a], points[b], (40, 220, 140), 2)
    for point in points.values():
        cv2.circle(frame, point, 4, (255, 255, 255), -1)
    def has_signal(indices):
        return bool(world) and all(i in points and min(world[i][3:]) >= 0.5 for i in indices)

    left = has_signal((23, 25, 27))
    right = has_signal((24, 26, 28))
    if not poses:
        status = "Step into the camera view"
    elif left and right:
        status = "Left and right knee signals available"
    elif left or right:
        status = "Left knee signal available" if left else "Right knee signal available"
    else:
        status = "Bring hips, knees and ankles into view"
    cv2.putText(frame, status, (12, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (255, 255, 255), 2)
    for side, name in enumerate(("Left", "Right")):
        missing = [label for joint, label in ((11, "shoulder"), (13, "elbow"), (15, "wrist"))
                   if joint + side not in points]
        arm = f"{name} arm: " + ("show " + ", ".join(missing) if missing else "shoulder, elbow, wrist visible")
        cv2.putText(frame, arm, (12, 51 + 21 * side), cv2.FONT_HERSHEY_SIMPLEX, 0.52,
                    (80, 210, 255) if missing else (80, 240, 160), 1, cv2.LINE_AA)
    window = "AthleteMind webcam - Q to stop"
    cv2.imshow(window, frame)
    key = cv2.waitKey(1) & 0xff
    return key not in (ord("q"), ord("Q"), 27) and cv2.getWindowProperty(window, cv2.WND_PROP_VISIBLE) >= 1


def camera_frames(camera_index, model, preview=False):
    import cv2
    import mediapipe as mp

    vision = mp.tasks.vision
    options = vision.PoseLandmarkerOptions(
        base_options=mp.tasks.BaseOptions(model_asset_path=str(model.resolve())),
        running_mode=vision.RunningMode.VIDEO,
        num_poses=1,
    )
    camera = cv2.VideoCapture(camera_index)
    try:
        if not camera.isOpened():
            raise RuntimeError(f"Could not open camera {camera_index}. Check Windows camera access for desktop apps, close other camera apps or try --camera 1.")
        camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        camera.set(cv2.CAP_PROP_FPS, 30)

        with vision.PoseLandmarker.create_from_options(options) as landmarker:
            last_capture_ms = -1
            while True:
                ok, frame = camera.read()
                if not ok:
                    raise RuntimeError(f"Could not read from camera {camera_index}.")

                capture_ms = max(time.monotonic_ns() // 1_000_000, last_capture_ms + 1)
                last_capture_ms = capture_ms
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
                result = landmarker.detect_for_video(image, capture_ms)
                world = encode_world_landmarks(result.pose_world_landmarks, result.pose_landmarks)
                if preview and not show_camera_preview(cv2, frame, result.pose_landmarks, world):
                    return
                yield capture_ms, world
    finally:
        camera.release()
        if preview:
            cv2.destroyAllWindows()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--synthetic", action="store_true", help="Send a simulated pose without a camera.")
    source.add_argument("--model", type=Path, help="Path to a Pose Landmarker .task model for webcam mode.")
    parser.add_argument("--camera", type=int, default=0, help="OpenCV camera index (default: 0).")
    parser.add_argument("--preview", action="store_true", help="Show the webcam and tracked joints locally.")
    parser.add_argument("--port", type=int, default=8765, help="Local Unreal UDP port (default: 8765).")
    parser.add_argument("--fps", type=int, default=30, help="Synthetic frame rate (default: 30).")
    parser.add_argument("--frames", type=int, help="Exit after sending this many frames.")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if args.camera < 0 or not 1 <= args.fps <= 60 or (args.frames is not None and args.frames < 1):
        parser.error("expected --camera >= 0, --fps between 1 and 60, and --frames >= 1")
    if args.model is not None and not args.model.is_file():
        parser.error(f"model file not found: {args.model}")

    session = str(uuid.uuid4())
    frames = synthetic_frames(args.fps) if args.synthetic else camera_frames(args.camera, args.model, args.preview)
    mode = "synthetic" if args.synthetic else f"camera {args.camera}"
    print(f"Sending {mode} pose frames to 127.0.0.1:{args.port}. Press Ctrl+C to stop.", flush=True)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            for sequence, (capture_ms, landmarks) in enumerate(itertools.islice(frames, args.frames)):
                send_pose(sock, args.port, session, sequence, capture_ms, landmarks)
    except KeyboardInterrupt:
        return 0
    except ImportError as error:
        print(f"{error}. Install the packages in PoseBridge/requirements.txt for webcam mode.", file=sys.stderr)
        return 1
    except (OSError, RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    finally:
        frames.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
