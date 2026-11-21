"""Compare the installed MediaPipe model against independently posed rig joints."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import sys

import cv2
import mediapipe as mp
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from send_pose import encode_world_landmarks


def render(xyz, faces, yaw, height):
    radians = np.deg2rad(yaw)
    near = np.array([np.sin(radians), np.cos(radians), 0.0])
    right = np.array([np.cos(radians), -np.sin(radians), 0.0])
    depth = 3.0 * height - xyz @ near
    scale = 375 * 3 / depth
    pixels = np.column_stack((320 + scale * (xyz @ right), 240 - scale * (xyz[:, 2] - height * 0.50)))
    triangles = xyz[faces]
    normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    lengths = np.maximum(np.linalg.norm(normals, axis=1), 1e-12)
    light = np.array([0.3, 0.7, 0.6])
    shade = (145 - 70 * (normals @ light) / lengths).clip(50, 230).astype(int)
    colors = [(v, v, v) for v in range(256)]
    frame = np.full((480, 640, 3), (70, 54, 45), np.uint8)
    polygons = np.rint(pixels[faces]).astype(np.int32)
    for index in np.argsort(-depth[faces].mean(axis=1)):
        cv2.fillConvexPoly(frame, polygons[index], colors[shade[index]])
    return frame


def estimated_flexion(world):
    if not world:
        return None
    leg = np.array([world[i] for i in (23, 25, 27)])
    if leg[:, 3:].min() < 0.5:
        return None
    a, b = leg[[0, 2], :3] - leg[1, :3]
    if min(a @ a, b @ b) < 1e-8:
        return None
    cosine = (a @ b) / (np.linalg.norm(a) * np.linalg.norm(b))
    return float(180 - np.rad2deg(np.arccos(np.clip(cosine, -1, 1))))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rig", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--model", type=Path, default=Path(__file__).resolve().parents[1] / "models" / "pose_landmarker_lite.task")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    asset = np.load(args.rig)
    vertices, faces, references = (asset[k] for k in ("vertices", "triangles", "reference"))
    height = vertices[0, :, 2].max() - vertices[0, :, 2].min()
    model = args.model.resolve()
    vision = mp.tasks.vision
    options = vision.PoseLandmarkerOptions(base_options=mp.tasks.BaseOptions(model_asset_path=str(model)),
                                          running_mode=vision.RunningMode.VIDEO, num_poses=1)
    sequence = (list(range(19)) + list(range(17, -1, -1))) * 2
    rows, summary, thumbnails = [], {}, []
    for condition, yaw in (("front", 0), ("oblique", 45), ("side", 90), ("oblique_legs_hidden", 45)):
        frames = [render(xyz, faces, yaw, height) for xyz in vertices]
        if condition.endswith("hidden"):
            for frame in frames:
                frame[240:] = (70, 54, 45)
        thumbnails.append(cv2.resize(frames[-1], (240, 180)))
        errors = []
        detected = 0
        with vision.PoseLandmarker.create_from_options(options) as detector:
            for idx, pose in enumerate(sequence):
                rgb = cv2.cvtColor(frames[pose], cv2.COLOR_BGR2RGB)
                result = detector.detect_for_video(mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb), round(idx*1000/12))
                world = encode_world_landmarks(result.pose_world_landmarks)
                detected += bool(world)
                angle = estimated_flexion(world)
                truth = float(references[pose])
                if angle is not None:
                    errors.append(angle - truth)
                rows.append((condition, idx, truth, bool(world), angle))
        summary[condition] = dict(frames=len(sequence), detected=detected, usable=len(errors),
                                  bias=float(np.mean(errors)) if errors else None,
                                  mae=float(np.mean(np.abs(errors))) if errors else None)
        print(condition, summary[condition], flush=True)
    with (args.output / "frames.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("view", "frame", "rig_flexion_deg", "detected", "estimated_flexion_deg"))
        writer.writerows(rows)
    report = dict(mediapipe=mp.__version__, model_sha256=hashlib.sha256(model.read_bytes()).hexdigest(),
                  reference_range_deg=[float(min(references)), float(max(references))], conditions=summary)
    (args.output / "summary.json").write_text(json.dumps(report, indent=2))
    cv2.imwrite(str(args.output / "views.jpg"), np.hstack(thumbnails))


if __name__ == "__main__":
    main()
