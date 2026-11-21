# AthleteMind

AthleteMind is a turn-based Unreal Engine 5.7 game that uses a webcam to turn a squat, elbow curl, or arm lift into a combat action. The game waits for the player during each movement. A C++ controller runs the encounter and a separate Python process sends MediaPipe pose landmarks to Unreal over loopback UDP.

## Play on Windows

1. Install Unreal Engine 5.7 and the Visual Studio C++ game development tools. Open `MediapipeCombat.uproject` and build the project modules if prompted.
2. Open `Content/AthleteMind/Lvl_RehabDuel`, press **Play**, and choose **Practice** to preview the moves with a mouse, keyboard, or gamepad.
3. For webcam play, run `python PoseBridge/start_webcam.py` in the project folder. The launcher installs its Python environment and downloads Google's Pose Landmarker Lite model on first use. Choose **Play with webcam**, select the side facing the camera, and let the game observe your starting stance.

**Relay Strike** uses a squat. **Pulse Ward** uses an elbow curl to block a pulse. **Signal Flare** builds charge with an arm lift. The default ranges are developer test settings. The project includes digital-rig comparisons and scripted game tests. Clinical validation with human reference measurements remains outstanding. [Controls and build notes](ATHLETEMIND_PLAN.md) · [Tracking validation](PoseBridge/validation/RESULTS.md)

## Local data and optional reports

Webcam landmarks travel to the game on the same computer. Webcam battles save their results under `Saved/RehabSessions`. If you configure a Gemini API key, the optional report worker sends a summary of the measured session to Google. Start Unreal with `-NoSessionReport` to disable this feature. Keep API keys in environment variables.

## Art assets

The public checkout uses the UE Third Person template characters and animations. If your project also contains the separately obtained Stone Golem and Combat Magic Anims packs and both retargeted clips, the duel selects that presentation automatically. This repository includes the AthleteMind arena, movement guide, audio and environment artwork. Epic's template assets and code retain their original terms. [Unreal Engine EULA, section 5(b)](https://www.unrealengine.com/eula/unreal)

## Run the checks

Build the Development Editor target first. Run these from PowerShell in the project root. The runtime tests launch separate Unreal processes and use synthetic local input.

```powershell
python PoseBridge/test_bridge_runtime.py
python PoseBridge/test_duel_runtime.py
python PoseBridge/test_hands_free_runtime.py
python PoseBridge/test_session_report.py
```

If Unreal reports that its cache path exceeds 119 characters, set `$env:ATHLETEMIND_TEST_DDC` to a shorter writable cache directory before running the runtime tests.
