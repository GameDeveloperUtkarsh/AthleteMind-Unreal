# Knee tracking on a controlled digital squat

**Decision.** Keep tactical choice, rest and spectacle in combat. Credit a completed movement cycle once. On uncertain tracking, preserve the choice and offer a pause or skip. The front view returned a knee estimate in every frame yet underestimated the deepest pose by about 61–62°. Awarding points for hitting a narrow angle band could prompt a player to go deeper simply because the camera underestimated the movement.

## Run and results

The test posed this project's Quinn mesh at 19 joint configurations and measured the left hip–knee–ankle angle from the evaluated rig. The reference flexion ranged from 1.1° to 91.0°. Each view used the same two bend-and-return cycles, 74 frames at 12 Hz and 640 × 480 resolution. MediaPipe received RGB pixels only. Tracking state was reset between conditions. The hidden condition replaced the bottom half of each oblique image with the background color. [Rendered views](results/views.jpg)

| Camera condition | Body detected | Knee passed the game's signal checks | Mean signed error | Mean absolute error |
| --- | ---: | ---: | ---: | ---: |
| Front | 74/74 | 74/74 | −24.6° | 28.0° |
| 45° toward measured side | 74/74 | 74/74 | −1.6° | 8.7° |
| Measured side, 90° | 74/74 | 74/74 | +1.0° | 7.6° |
| 45°, legs hidden | 74/74 | 0/74 | Undefined | Undefined |

Errors use estimated minus reference flexion. Both values use zero at full extension. A usable frame passed the sender's world-landmark encoding, had visibility and presence of at least 0.5 at all three landmarks, and had nondegenerate limb vectors. The offline scorer mirrors the native angle calculation. The project's separate headless Unreal test passed with known 90° and 180° inputs, invalid packets, low confidence, signal loss and reconnection. [Per-frame comparisons](results/frames.csv) · [Angle traces](results/flexion.png) · [Model hash and run summary](results/summary.json)

At the two deepest frames, the front view read 29.0° and 30.1° against the same 91.0° reference. Side readings were 79.8° and 81.0°. Even the side view underread at maximum flexion. Average bias can hide errors that matter at a gameplay threshold.

## What the playable duel credited

On November 21, 2026, the hands-free regression replayed a side-view squat rendered from the Quinn rig through the Full model, the webcam sender's current image-boundary checks and the native Unreal receiver. The default Relay strike was highlighted. The movement selected it and completed one rep with zero tracking interruptions. Peak estimated knee flexion was 82.5°. In a separate 10-frame sample at each of 45° and 90°, the measured elbow, wrist and knee all passed the confidence checks in every frame. The preview drew the elbow-to-wrist segments. Moving a projected wrist outside the image excluded that wrist while preserving the knee signal. These checks used a digital rig and the Full model. Live-camera performance with the deployed Lite model still needs measurement.

We rendered one additional squat from the same rig in two views, passed each RGB sequence through the same Full benchmark bundle, then replayed all 33 estimated landmarks at 12 Hz through UDP into Unreal's `Lvl_RehabDuel`. Guard was selected for both runs. Each 66-frame sequence began with 12 neutral frames, bent to the deepest pose, held it for six extra frames, returned and held neutral for 12 extra frames. The duel used the default left-knee developer profile.

| View of the same movement | Usable knee estimates | Peak estimated flexion | Guard turns credited |
| --- | ---: | ---: | ---: |
| Camera on measured side | 66 / 66 | 82.7° | 1 |
| Camera in front | 66 / 66 | 31.2° | 0 |

The front estimate stayed below the fixture's 50° entry threshold for the whole movement even though every sample passed the confidence checks. The duel now prompts the player to put the camera on the measured side before selecting a response. A real-person comparison with the deployed Lite model is still needed to evaluate that setup.

## What this run covers

The inference runtime was MediaPipe 0.10.35 in VIDEO mode. Windows denied the benchmark process read access to the installed Lite file. For this run a separate task bundle was constructed from the detector and Full landmark weights shipped in the cached mediapipe 0.10.14 wheel, with float input normalization metadata added for the Tasks API. Production continues to use its own Lite model. The comparison figures apply only to this benchmark bundle.

The sample is one rendered rig under fixed lighting. The two cycles repeat the same poses. These 296 observations are correlated frames, with no human participants or independently measured patient anatomy. The test measures image-to-angle behavior under controlled conditions. Rep-recognition accuracy, patient engagement and clinical agreement remain open questions.

## Reproduce and extend

In this checkout the prepared fixture is `Saved/quinn_clean.npz`. To test the exact deployed Lite model from a PowerShell session that can read it, run this from the project root. Use a new output folder for each model or changed test configuration.

```powershell
.\PoseBridge\.venv\Scripts\python.exe .\PoseBridge\validation\compare_views.py .\Saved\quinn_clean.npz .\Saved\PoseValidation\Lite
```

To regenerate the fixture, run `export_quinn.py` with Unreal's Python Script and glTF Exporter plugins enabled. It exports the project's skinned mesh to `Saved/ValidationQuinn.glb`. Run Blender with `-b --factory-startup --python-exit-code 1 -P PoseBridge/validation/pose_rig.py -- Saved/ValidationQuinn.glb Saved/quinn_clean.npz`. The generator reads the deformed skin and measures reference angles independently of the commanded rotations.

For human validation, compare one synchronized camera stream and corresponding motion-capture reference at a time. Match the measured side, align timestamps, and document the angle convention, anatomical model and any offset before calculating errors. Report available-frame coverage, peak error and signed error by trial and participant. Have an independent reviewer label complete and interrupted reps. The OpenCap laboratory validation dataset includes squat video and reference measurements. External dataset downloads were unavailable for this run. [OpenCap validation study](https://doi.org/10.1371/journal.pcbi.1011462) · [Authors' validation workflow](https://github.com/opencap-org/opencap-core/blob/main/ReproducePaperResults/labValidationVideosToKinematics.py)
