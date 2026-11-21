"""Generate a short Gemini recap for one completed AthleteMind session."""

import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from urllib.error import HTTPError
from urllib.request import Request, urlopen


def report_facts(session):
    facts = {name: session[name] for name in (
        "planned", "completed", "side", "skipped", "interruptions", "dodges", "finish_reason"
    )}
    exercises = {}
    for rep in session["reps"]:
        name = rep["exercise"]
        angle_field = "estimated_peak_arm_elevation_deg" if name == "arm_lift" else "estimated_peak_flexion_deg"
        readings = exercises.setdefault(name, [])
        readings.append(round(rep[angle_field], 1))
    facts["estimated_peak_degrees_by_exercise"] = exercises
    return facts


def format_report(recap, facts):
    lines = [recap, "", "Recorded session results",
             f"Completed {facts['completed']} of {facts['planned']} planned repetitions on the {facts['side']} side.",
             f"Skipped turns: {facts['skipped']}. Tracking interruptions: {facts['interruptions']}. Dodges: {facts['dodges']}.",
             f"Session ended: {facts['finish_reason']}."]
    for exercise, peaks in facts["estimated_peak_degrees_by_exercise"].items():
        metric = "arm elevation" if exercise == "arm_lift" else "flexion"
        values = ", ".join(f"{peak:g}°" for peak in peaks)
        lines.append(f"{exercise.replace('_', ' ').capitalize()} ({len(peaks)} reps), estimated peak {metric}: {values}.")
    return "\n".join(lines) + "\n"


def generate_report(facts, key, model, transport=urlopen):
    if not re.fullmatch(r"[a-zA-Z0-9._-]+", model):
        raise ValueError("Invalid GEMINI_MODEL.")
    instruction = (
        "Write a brief end-of-session recap for the player of a movement-controlled game. "
        "Use only the supplied counts, exercises and finish reason. In two plain sentences "
        "of at most 22 words each, describe the set and any recorded interruption or dodge. "
        "A dodge is separate from an exercise repetition. Avoid medical assessments, advice, "
        "motion quality claims and inferred feelings. Return only the recap."
    )
    body = {"systemInstruction": {"parts": [{"text": instruction}]},
            "contents": [{"role": "user", "parts": [{"text": json.dumps(facts)}]}]}
    request = Request(
        f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent",
        data=json.dumps(body).encode(), headers={"Content-Type": "application/json", "x-goog-api-key": key},
    )
    with transport(request, timeout=35) as response:
        payload = json.load(response)
    candidates = payload.get("candidates") or []
    if not candidates or candidates[0].get("finishReason") != "STOP":
        raise ValueError("Gemini did not return a complete report.")
    parts = candidates[0].get("content", {}).get("parts", [])
    recap = " ".join(" ".join(part.get("text", "") for part in parts if not part.get("thought")).split())
    if not recap or len(recap) > 650:
        raise ValueError("Gemini returned an empty or oversized report.")
    return recap


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=Path)
    path = parser.parse_args().summary
    key = os.environ.get("GEMINI_API_KEY") or os.environ.get("GOOGLE_API_KEY")
    if not key:
        return 2
    model = os.environ.get("GEMINI_MODEL") or "gemini-3.8-flash"
    output = path.with_suffix(".report.txt")
    temp = None
    try:
        facts = report_facts(json.loads(path.read_text(encoding="utf-8-sig")))
        report = generate_report(facts, key, model)
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent, delete=False) as stream:
            temp = Path(stream.name)
            stream.write(format_report(report, facts))
        temp.replace(output)
        return 0
    except HTTPError as error:
        print(f"Gemini HTTP {error.code}", file=sys.stderr)
        return 3 if error.code in (401, 403) else 1
    except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
        print(f"Session report unavailable ({type(error).__name__}).", file=sys.stderr)
        return 1
    finally:
        if temp:
            temp.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
