#include "Rehab/RehabDuel.h"

#include "Components/InputComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Rehab/DuelWarden.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogRehabDuel, Log, All);

ARehabDuelMode::ARehabDuelMode()
{
	PlayerControllerClass = ARehabDuelController::StaticClass();
	HUDClass = ARehabDuelHUD::StaticClass();
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
}

ARehabDuelController::ARehabDuelController()
{
	bAutoManageActiveCameraTarget = false;
}

void ARehabDuelController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;
	bRightSide = Profile.bMeasureRight;
	Bridge = GetGameInstance()->GetSubsystem<UPoseBridgeSubsystem>();
	Bridge->SetPoseOverlayHidden(true);
	Bridge->OnAcceptedPose.AddUObject(this, &ThisClass::ReadPose);
	Warden = GetWorld()->SpawnActor<ADuelWarden>();
	if (Warden) { Warden->SetCameraMotion(bCameraMotion); SetViewTarget(Warden); }
	bShowMouseCursor = bEnableClickEvents = bEnableMouseOverEvents = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
}

void ARehabDuelController::SetupInputComponent()
{
	Super::SetupInputComponent();
	for (const FKey& Key : { EKeys::Gamepad_DPad_Right, EKeys::Gamepad_DPad_Down })
		InputComponent->BindKey(Key, IE_Pressed, this, &ThisClass::PadNext);
	for (const FKey& Key : { EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Up })
		InputComponent->BindKey(Key, IE_Pressed, this, &ThisClass::PadPrevious);
	InputComponent->BindKey(EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, this, &ThisClass::PadConfirm);
	InputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this, &ThisClass::PadPause);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &ThisClass::DuelSide);
	InputComponent->BindKey(EKeys::M, IE_Pressed, this, &ThisClass::DuelMotion);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ThisClass::DuelWard);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ThisClass::DuelFlare);
	InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ThisClass::DuelHandsFree);
	InputComponent->BindKey(EKeys::C, IE_Pressed, this, &ThisClass::DuelLive);
	InputComponent->BindKey(EKeys::W, IE_Pressed, this, &ThisClass::DuelConnect);
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ThisClass::DuelPractice);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ThisClass::DuelGuard);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ThisClass::DuelCounter);
	InputComponent->BindKey(EKeys::Enter, IE_Released, this, &ThisClass::DuelContinue);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ThisClass::DuelReport);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ThisClass::DuelRestart);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ThisClass::DuelPause);
	InputComponent->BindKey(EKeys::K, IE_Pressed, this, &ThisClass::DuelSkip);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ThisClass::DuelFinish);
	InputComponent->BindKey(EKeys::D, IE_Pressed, this, &ThisClass::DuelDiscomfort);
}

void ARehabDuelController::ClearMovement()
{
	Cycle.Reset();
	PoseCorrection.Reset();
	bPauseArmed = false;
	PauseStart = PauseLast = PauseReachStart = -1;
	LastSequence = -1;
	bRecoveringSignal = false;
	SenderId.Invalidate();
	LastCapture = -1;
	LastArrival = 0;
	PeakFlexion = 0;
	AngleSamples = 0;
}

void ARehabDuelController::DuelPractice()
{
	if (Step != EDuelStep::Intro && Step != EDuelStep::Setup) return;
	StopOwnedCamera();
	NewSet(true);
}

void ARehabDuelController::DuelLive()
{
	if (Step != EDuelStep::Intro) return;
	Step = EDuelStep::Setup;
	SetupStart = SetupLast = -1;
	SetupNeutral = -1;
	Outcome.Reset();
	UE_LOG(LogRehabDuel, Display, TEXT("Camera setup opened"));
}

void ARehabDuelController::DuelSide()
{
	if (Step != EDuelStep::Setup) return;
	bRightSide = !bRightSide;
	SetupStart = SetupLast = -1;
	SetupNeutral = -1;
	GestureLast = -1;
	bGestureArmed = false;
	GestureFill = 0;
}

void ARehabDuelController::DuelMotion()
{
	if (Step != EDuelStep::Paused && Step != EDuelStep::Intro) return;
	bCameraMotion = !bCameraMotion;
	if (Warden) Warden->SetCameraMotion(bCameraMotion);
	auto* Defaults = GetMutableDefault<ARehabDuelController>();
	Defaults->bCameraMotion = bCameraMotion;
	Defaults->SaveConfig();
}

bool ARehabDuelController::HasKneeSignal() const
{
	const int32 Side = bRightSide ? 1 : 0;
	float Included = 0;
	return Bridge && Bridge->TryGetLatestJointAngle(23+Side, 25+Side, 27+Side, Profile.MinimumConfidence, Included);
}

void ARehabDuelController::ObserveSetupStance(const FPoseBridgeFrame& Frame)
{
	if (!bLearnStartingStance) return;
	const int32 S = bRightSide ? 1 : 0;
	float Included = 0;
	const int64 T = Frame.CaptureTimeMs;
	if (!Frame.bTracked || !Bridge->TryGetLatestJointAngle(23+S,25+S,27+S,Profile.MinimumConfidence,Included))
	{
		SetupStart = -1;
		SetupNeutral = -1;
		return;
	}
	const float Angle = 180.f-Included;
	const float Limit = FMath::Min(Profile.NeutralMax+10.f, Profile.TargetMin-12.f);
	if (SetupSender != Frame.SessionId || T-SetupLast > 250 || Angle > Limit || FMath::Abs(Angle-SetupAnchor)>4.f)
	{
		SetupStart = -1;
		SetupNeutral = -1;
	}
	SetupSender = Frame.SessionId;
	SetupLast = T;
	if (Angle > Limit) return;
	if (SetupStart < 0) { SetupStart = T; SetupAnchor = Angle; }
	if (T-SetupStart >= 500) SetupNeutral = .5f*(SetupAnchor+Angle);
}

void ARehabDuelController::DuelConnect()
{
	if (Step != EDuelStep::Setup || CameraProcess.IsValid() || !Bridge || Bridge->GetListeningPort() == 0) return;
	const auto Signal = Bridge->GetTrackingState();
	if (Signal == EPoseBridgeState::NoPose || Signal == EPoseBridgeState::Tracked) return;
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("PoseBridge"));
	const FString Python = Dir / TEXT(".venv/Scripts/python.exe");
	const FString Script = Dir / TEXT("send_pose.py");
	const FString Model = Dir / TEXT("models/pose_landmarker_lite.task");
	if (FPaths::FileExists(Python) && FPaths::FileExists(Model) && FPaths::FileExists(Script))
	{
		const FString Args = FString::Printf(TEXT("\"%s\" --model \"%s\" --port %d"), *Script, *Model, Bridge->GetListeningPort());
		CameraProcess = FPlatformProcess::CreateProc(*Python, *Args, false, true, true, nullptr, 0, *Dir, nullptr);
	}
	Outcome = CameraProcess.IsValid() ? TEXT("Opening your webcam...") : TEXT("From the project folder, run python PoseBridge/start_webcam.py");
}

void ARehabDuelController::StopOwnedCamera()
{
	if (!CameraProcess.IsValid()) return;
	if (FPlatformProcess::IsProcRunning(CameraProcess)) FPlatformProcess::TerminateProc(CameraProcess, true);
	FPlatformProcess::CloseProc(CameraProcess);
	CameraProcess.Reset();
}

void ARehabDuelController::NewSet(bool bKeyboard)
{
	bPractice = bKeyboard;
	ReportText.Reset();
	bReportReady = false;
	KneeSession = Profile;
	if (!bPractice && bLearnStartingStance && SetupNeutral >= 0)
		KneeSession.NeutralMax = FMath::Max(Profile.NeutralMax,FMath::Min(SetupNeutral+4.f,Profile.TargetMin-10.f));
	bHandsFree = false;
	Wards = 0;
	Move = EDuelMove::Strike;
	if (!bPractice && !Profile.IsValid())
	{
		Step = EDuelStep::Finished;
		Outcome = TEXT("Invalid squat test profile. Check DefaultGame.ini.");
		return;
	}
	SessionId = bPractice ? FGuid() : FGuid::NewGuid();
	Reps.Reset();
	PracticeTurns = 0;
	Skipped = Interruptions = 0;
	bEnemyTurn = false;
	Dodges = SkippedDodges = PreviewPhase = 0;
	Charge = 1;
	MaxHull = Hull = bPractice ? 2 * TurnLimit() : 4;
	LastDamage = 0;
	if (Warden)
	{
		Warden->ResetEncounter();
		Warden->SetRelayLit(false);
	}
	ClearMovement();
	Step = EDuelStep::Choose;
	bSaved = false;
	Outcome.Reset();
	FinishReason.Reset();
	if (bPractice)
	{
		UE_LOG(LogRehabDuel, Display, TEXT("Practice battle ready"));
	}
	else
	{
		UE_LOG(LogRehabDuel, Display, TEXT("Duel ready: %s"), *SessionId.ToString());
	}
}

void ARehabDuelController::Choose(EDuelMove Choice)
{
	if (Step != EDuelStep::Choose || (Choice == EDuelMove::Charged && Charge == 0)) return;
	if (Choice == EDuelMove::Ward && (!bEnableArmMoves || !CurlProfile.IsValid())) return;
	if (Choice == EDuelMove::Flare && (!bEnableArmMoves || !LiftProfile.IsValid())) return;
	Move = Choice;
	PreviewPhase = 0;
	ClearMovement();
	Outcome.Reset();
	Step = EDuelStep::Move;
	const TCHAR* Names[] = {TEXT("Guard"), TEXT("Counter"), TEXT("Ward"), TEXT("Flare")};
	UE_LOG(LogRehabDuel, Display, TEXT("Response selected: %s"), Names[static_cast<int32>(Move)]);
}

void ARehabDuelController::DuelGuard() { Choose(EDuelMove::Strike); }
void ARehabDuelController::DuelCounter() { Choose(EDuelMove::Charged); }
void ARehabDuelController::DuelWard() { Choose(EDuelMove::Ward); }
void ARehabDuelController::DuelFlare() { Choose(EDuelMove::Flare); }

const FSquatProfile& ARehabDuelController::MovementProfile() const
{
	if (!bEnemyTurn && Move == EDuelMove::Ward) return CurlProfile;
	if (!bEnemyTurn && Move == EDuelMove::Flare) return LiftProfile;
	return KneeSession;
}

FIntVector ARehabDuelController::MeasuredJoints() const
{
	const int32 S = bRightSide ? 1 : 0;
	if (!bEnemyTurn && Move == EDuelMove::Flare) return {23+S, 11+S, 13+S};
	if (!bEnemyTurn && Move == EDuelMove::Ward) return {11+S, 13+S, 15+S};
	return {23+S, 25+S, 27+S};
}

bool ARehabDuelController::ReadJoint(float& Angle) const
{
	const FIntVector J = MeasuredJoints();
	if (!Bridge || !Bridge->TryGetLatestJointAngle(J.X, J.Y, J.Z, MovementProfile().MinimumConfidence, Angle)) return false;
	if (bEnemyTurn || Move != EDuelMove::Flare) Angle = 180.f - Angle;
	return true;
}

FString ARehabDuelController::ArmPoseCue(const FPoseBridgeFrame& Frame, float Angle) const
{
	if (bEnemyTurn || (Move != EDuelMove::Ward && Move != EDuelMove::Flare)) return {};
	const int32 S = bRightSide ? 1 : 0;
	const auto& Points = Frame.Landmarks;
	const FSquatProfile& P = MovementProfile();
	auto Visible = [&](int32 I) { return FMath::Min(Points[I].Visibility, Points[I].Presence) >= P.MinimumConfidence; };
	if (!Visible(23+S)) return TEXT("Bring your measured hip into view.");
	const FVector Up = (Points[11+S].WorldMeters - Points[23+S].WorldMeters).GetSafeNormal();
	const FVector Arm = (Points[13+S].WorldMeters - Points[11+S].WorldMeters).GetSafeNormal();
	if (Up.IsNearlyZero() || Arm.IsNearlyZero()) return TEXT("Bring your upper body into view.");
	if (Move == EDuelMove::Ward)
		return FVector::DotProduct(-Up,Arm) < FMath::Cos(FMath::DegreesToRadians(30.f))
			? TEXT("Lower your elbow alongside your ribs.") : FString();
	if (Angle <= P.NeutralMax) return {};
	// Anatomical left-to-right and hip-to-shoulder define forward in MediaPipe world axes.
	FVector Across;
	if (Visible(11) && Visible(12)) Across = Points[12].WorldMeters - Points[11].WorldMeters;
	else if (Visible(23) && Visible(24)) Across = Points[24].WorldMeters - Points[23].WorldMeters;
	else return TEXT("Bring both shoulders or both hips into view.");
	const FVector Forward = FVector::CrossProduct(Up, Across).GetSafeNormal();
	const FVector Direction = FVector::VectorPlaneProject(Arm, Up).GetSafeNormal();
	if (Forward.IsNearlyZero() || Direction.IsNearlyZero()) return TEXT("Bring your shoulders and hips into view.");
	return FVector::DotProduct(Forward,Direction) < FMath::Cos(FMath::DegreesToRadians(45.f))
		? TEXT("Lower your arm, then lift it in front of you.") : FString();
}

void ARehabDuelController::ReadPose(const FPoseBridgeFrame& Frame)
{
	if (Step == EDuelStep::Setup) ObserveSetupStance(Frame);
	bool bSquatChoice = false;
	if (GestureMenu())
	{
		const EDuelStep Before = Step;
		ReadMenuGesture(Frame);
		// Accept movement selection only while the live menu explicitly offers a squat.
		if (Before != EDuelStep::Choose || Step != Before || FocusedAction() != "DuelGuard") return;
		bSquatChoice = true;
		Move = EDuelMove::Strike;
	}
	if ((!bSquatChoice && Step != EDuelStep::Move) || bPractice) return;
	float Flexion = 0;
	if (!Frame.bTracked || !ReadJoint(Flexion))
	{
		// A missing sample breaks a timed hold, but a brief blink keeps the exercise phase.
		Cycle.BreakHold();
		AngleSamples = 0;
		bPauseArmed = false;
		PauseStart = PauseReachStart = -1;
		if (LastCapture >= 0 && (Frame.SessionId != SenderId || Frame.CaptureTimeMs - LastCapture > 750)) Interrupt();
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (LastCapture >= 0 && (Frame.SessionId != SenderId || Frame.CaptureTimeMs <= LastCapture ||
		Frame.CaptureTimeMs - LastCapture > 750 || Now - LastArrival > 0.75))
	{
		Interrupt();
	}
	const int64 Gap = Frame.CaptureTimeMs - LastCapture;
	if (LastCapture >= 0 && (Gap > 350 || Now-LastArrival > .35 || Frame.Sequence-LastSequence != 1))
	{
		Cycle.BreakHold();
		AngleSamples = 0;
		// A materially different position after a longer blind interval needs a fresh start.
		if ((Gap > 350 || Now-LastArrival > .35) && FMath::Abs(Flexion-LastRawAngle) > 12.f) Interrupt();
	}
	const float Raw = Flexion;
	if (AngleSamples >= 2) Flexion = FMath::Clamp(Raw, FMath::Min(OlderRawAngle,LastRawAngle), FMath::Max(OlderRawAngle,LastRawAngle));
	OlderRawAngle = LastRawAngle;
	LastRawAngle = Raw;
	AngleSamples = FMath::Min(2, AngleSamples+1);
	LastSequence = Frame.Sequence;
	SenderId = Frame.SessionId;
	LastCapture = Frame.CaptureTimeMs;
	LastArrival = Now;
	PoseCorrection = ArmPoseCue(Frame, Raw);
	if (!PoseCorrection.IsEmpty())
	{
		Cycle.Reset();
		PeakFlexion = 0;
		AngleSamples = 0;
		return;
	}
	if (!bSquatChoice && bHandsFree && TryGesturePause(Frame, Flexion)) return;
	const FSquatProfile& P = MovementProfile();
	if (Flexion <= P.NeutralMax) bRecoveringSignal = false;
	if (bEnemyTurn)
	{
		if (Cycle.HoldStartingStance(KneeSession, Flexion, LastCapture, Profile.DodgeHoldMs)) ResolveDodge();
		return;
	}
	const ESquatPhase PreviousPhase = Cycle.Phase;
	const bool bComplete = Cycle.Observe(P, Flexion, LastCapture);
	if (bSquatChoice && Cycle.Phase == ESquatPhase::Holding)
	{
		// Continue the very same repetition. Choose() would discard the observed start.
		Step = EDuelStep::Move;
		Outcome.Reset();
		UE_LOG(LogRehabDuel, Display, TEXT("Squat selected Relay strike"));
	}
	if (PreviousPhase == ESquatPhase::FindNeutral) PeakFlexion = 0;
	if (Cycle.Phase != ESquatPhase::FindNeutral || bComplete)
	{
		PeakFlexion = FMath::Max(PeakFlexion, Flexion);
	}
	if (!bComplete) return;
	ResolveResponse();
}

int32 ARehabDuelController::DamageFor(EDuelMove Choice) const
{
	if (Choice == EDuelMove::Ward || Choice == EDuelMove::Flare) return 1;
	if (Choice == EDuelMove::Charged) return CoreExposed() ? 3 : 2;
	return CoreExposed() ? 1 : 2;
}

void ARehabDuelController::ResolveResponse()
{
	LastDamage = FMath::Min(Hull, DamageFor(Move));
	Hull -= LastDamage;
	if (bPractice) ++PracticeTurns;
	else Reps.Add(FScoredRep{Move, PeakFlexion});
	switch (Move)
	{
	case EDuelMove::Ward: ++Wards; Outcome = TEXT("Pulse ward cast"); break;
	case EDuelMove::Flare: Charge += 2; Outcome = TEXT("Signal flare landed"); break;
	case EDuelMove::Charged: --Charge; Outcome = TEXT("Charged attack landed"); break;
	default: ++Charge; Outcome = TEXT("Relay strike landed"); break;
	}
	if (Warden) Warden->ResolveStrike(Move);
	UE_LOG(LogRehabDuel, Display, TEXT("Resolved %s %d: %s | Hull %d | Charge %d"),
		bPractice ? TEXT("practice turn") : TEXT("rep"), Reps.Num() + PracticeTurns, *Outcome, Hull, Charge);
	ResolveTime = 0;
	ClearMovement();
	Step = EDuelStep::Resolve;
}

void ARehabDuelController::ResolveDodge()
{
	++Dodges;
	LastDamage = 0;
	Outcome = TEXT("Pulse dodged");
	if (Warden) Warden->PlayDodge();
	UE_LOG(LogRehabDuel, Display, TEXT("Resolved dodge %d"), Dodges);
	ResolveTime = 0;
	ClearMovement();
	Step = EDuelStep::Resolve;
}

void ARehabDuelController::PlayerTick(float DeltaSeconds)
{
	Super::PlayerTick(DeltaSeconds);
	if (ReportProcess.IsValid()) PollReport();
	if (FocusStep != Step) { FocusStep = Step; FocusAction = NAME_None; }
	if (GestureStep != Step || FPlatformTime::Seconds()-GestureArrival > .35)
	{
		GestureLast = -1;
		GestureFill = 0;
		bGestureArmed = false;
		Candidate = EDuelGesture::None;
	}
	if (CameraProcess.IsValid() && !FPlatformProcess::IsProcRunning(CameraProcess))
	{
		StopOwnedCamera();
		Outcome = TEXT("Webcam closed. Check camera access or close other camera apps, then retry.");
	}
	if (Step == EDuelStep::Resolve)
	{
		ResolveTime += DeltaSeconds;
		if (ResolveTime >= ADuelWarden::StrikeDuration) CloseTurn();
	}
	if (Step == EDuelStep::Move && LastCapture >= 0 && FPlatformTime::Seconds() - LastArrival > 0.75)
	{
		Interrupt();
	}
}

void ARehabDuelController::Interrupt()
{
	if (Step == EDuelStep::Choose) { ClearMovement(); return; }
	++Interruptions;
	ClearMovement();
	bRecoveringSignal = true;
	UE_LOG(LogRehabDuel, Display, TEXT("Tracking gap: waiting for a fresh starting stance"));
}

void ARehabDuelController::DuelPause()
{
	if (Step != EDuelStep::Choose && Step != EDuelStep::Move && Step != EDuelStep::Rest) return;
	ResumeStep = Step;
	Step = EDuelStep::Paused;
	ClearMovement();
	Outcome = TEXT("Take the time you need.");
}

void ARehabDuelController::DuelRestart()
{
	if (Step == EDuelStep::Finished)
	{
		if (SessionId.IsValid() && !bSaved) SaveSummary();
		if (!SessionId.IsValid() || bSaved)
		{
			// The worker owns its output and may finish after the player leaves this screen.
			if (ReportProcess.IsValid()) { FPlatformProcess::CloseProc(ReportProcess); ReportProcess.Reset(); }
			SessionId.Invalidate();
			Step = EDuelStep::Intro;
			Outcome.Reset();
			if (Warden)
			{
				Warden->ResetEncounter();
				Warden->SetRelayLit(false);
			}
		}
	}
}

void ARehabDuelController::DuelContinue()
{
	// A single confirm must not both open a movement card and advance its first cue.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastContinueTime < .14) return;
	LastContinueTime = Now;
	if (Step == EDuelStep::Intro) { DuelPractice(); return; }
	if (Step == EDuelStep::Setup && HasKneeSignal()) { NewSet(false); return; }
	if (Step == EDuelStep::Move && bPractice)
	{
		if (bEnemyTurn) ResolveDodge();
		else if (PreviewPhase < 3) ++PreviewPhase;
		else ResolveResponse();
		return;
	}
	if (Step == EDuelStep::Paused || Step == EDuelStep::Rest)
	{
		if (Step == EDuelStep::Paused) Step = ResumeStep;
		else
		{
			bEnemyTurn = !bEnemyTurn && LastDamage > 0 && Move != EDuelMove::Ward;
			Step = bEnemyTurn ? EDuelStep::Move : EDuelStep::Choose;
			if (bEnemyTurn)
			{
				ClearMovement();
				if (Warden) Warden->SetDodgeReady(true);
				UE_LOG(LogRehabDuel, Display, TEXT("Dodge cue ready"));
			}
		}
		Outcome.Reset();
		UE_LOG(LogRehabDuel, Display, TEXT("Continued encounter"));
	}
}

void ARehabDuelController::DuelSkip()
{
	if (Step != EDuelStep::Move && !(Step == EDuelStep::Paused && ResumeStep == EDuelStep::Move)) return;
	if (bEnemyTurn) ++SkippedDodges;
	else ++Skipped;
	LastDamage = 0;
	Outcome = bEnemyTurn ? TEXT("Dodge skipped. Your exercise count is unchanged.") : TEXT("Attack skipped. Take a rest.");
	CloseTurn();
}

void ARehabDuelController::CloseTurn()
{
	ClearMovement();
	if (Warden) Warden->SetDodgeReady(false);
	Step = EDuelStep::Rest;
	if (Reps.Num() + PracticeTurns + Skipped >= TurnLimit()) Finish(TEXT("Set limit reached"));
	else if (Hull == 0) Finish(TEXT("Relay restored"));
}

void ARehabDuelController::DuelFinish() { Finish(TEXT("Finished early")); }
void ARehabDuelController::DuelDiscomfort()
{
	if (!bPractice) Finish(TEXT("Stopped for reported discomfort"));
}

void ARehabDuelController::Finish(const FString& Reason)
{
	if (Step == EDuelStep::Finished || Step == EDuelStep::Intro || Step == EDuelStep::Setup) return;
	Step = EDuelStep::Finished;
	FinishReason = Reason;
	if (Warden) Warden->SetDodgeReady(false);
	if (Hull == 0 && Warden) Warden->SetRelayLit(true);
	ClearMovement();
	StopOwnedCamera();
	if (!bPractice)
	{
		SaveSummary();
		if (bSaved) StartReport();
	}
}

void ARehabDuelController::DuelReport()
{
	if (Step != EDuelStep::Finished || bPractice || !SessionId.IsValid() || ReportProcess.IsValid() || bReportReady) return;
	if (!bSaved) SaveSummary();
	if (bSaved) StartReport();
}

void ARehabDuelController::StartReport()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("NoSessionReport")))
	{
		ReportText = TEXT("Gemini reporting is off for this run.");
		return;
	}
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("PoseBridge"));
	const FString Script = Dir / TEXT("session_report.py");
	const FString Summary = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("RehabSessions") / (SessionId.ToString() + TEXT(".json")));
	const FString LocalPython = Dir / TEXT(".venv/Scripts/python.exe");
	const FString Python = FPaths::FileExists(LocalPython) ? LocalPython : FString(TEXT("python.exe"));
	const FString Args = FString::Printf(TEXT("\"%s\" \"%s\""), *Script, *Summary);
	ReportProcess = FPlatformProcess::CreateProc(*Python, *Args, false, true, true, nullptr, 0, *Dir, nullptr);
	ReportText = ReportProcess.IsValid() ? TEXT("Preparing your Gemini report...") : TEXT("Could not start Python. Your session is saved.");
}

void ARehabDuelController::PollReport()
{
	int32 Code = 0;
	if (!FPlatformProcess::GetProcReturnCode(ReportProcess, &Code)) return;
	FPlatformProcess::CloseProc(ReportProcess);
	ReportProcess.Reset();
	const FString Path = FPaths::ProjectSavedDir() / TEXT("RehabSessions") / (SessionId.ToString() + TEXT(".report.txt"));
	bReportReady = Code == 0 && FFileHelper::LoadFileToString(ReportText, *Path) && !ReportText.TrimStartAndEnd().IsEmpty();
	if (bReportReady)
	{
		int32 FirstLineEnd;
		if (ReportText.FindChar('\n', FirstLineEnd)) ReportText.LeftInline(FirstLineEnd);
		ReportText.TrimStartAndEndInline();
	}
	else if (Code == 2) ReportText = TEXT("Set GEMINI_API_KEY and relaunch to enable Gemini reports. Your session is saved.");
	else if (Code == 3) ReportText = TEXT("Gemini rejected the API key. Your session is saved.");
	else ReportText = TEXT("Gemini report unavailable. Your session is saved.");
	UE_LOG(LogRehabDuel, Display, TEXT("Gemini report worker finished (code %d)."), Code);
}

void ARehabDuelController::SaveSummary()
{
	auto Summary = MakeShared<FJsonObject>();
	Summary->SetNumberField(TEXT("version"), 3);
	Summary->SetNumberField(TEXT("wards_cast"), Wards);
	Summary->SetBoolField(TEXT("hands_free_menus"), bHandsFree);
	Summary->SetStringField(TEXT("session"), SessionId.ToString());
	Summary->SetStringField(TEXT("profile"), TEXT("developer_duel"));
	Summary->SetStringField(TEXT("side"), bRightSide ? TEXT("right") : TEXT("left"));
	Summary->SetNumberField(TEXT("target_min_flexion_deg"), Profile.TargetMin);
	Summary->SetNumberField(TEXT("target_max_flexion_deg"), Profile.TargetMax);
	Summary->SetNumberField(TEXT("neutral_max_flexion_deg"), KneeSession.NeutralMax);
	Summary->SetNumberField(TEXT("neutral_hold_ms"), Profile.NeutralHoldMs);
	Summary->SetNumberField(TEXT("target_hold_ms"), Profile.TargetHoldMs);
	Summary->SetNumberField(TEXT("dodge_hold_ms"), Profile.DodgeHoldMs);
	Summary->SetNumberField(TEXT("dodges"), Dodges);
	Summary->SetNumberField(TEXT("skipped_dodges"), SkippedDodges);
	Summary->SetNumberField(TEXT("minimum_confidence"), Profile.MinimumConfidence);
	Summary->SetNumberField(TEXT("planned"), Profile.PlannedReps);
	Summary->SetNumberField(TEXT("completed"), Reps.Num());
	Summary->SetNumberField(TEXT("warden_hull_remaining"), Hull);
	Summary->SetNumberField(TEXT("charge_remaining"), Charge);
	Summary->SetNumberField(TEXT("skipped"), Skipped);
	Summary->SetNumberField(TEXT("interruptions"), Interruptions);
	Summary->SetStringField(TEXT("finish_reason"), FinishReason);
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const FScoredRep& Rep : Reps)
	{
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("rep"), Rows.Num()+1);
		const TCHAR* Responses[] = {TEXT("relay_strike"), TEXT("charged_attack"), TEXT("pulse_ward"), TEXT("signal_flare")};
		Entry->SetStringField(TEXT("response"), Responses[static_cast<int32>(Rep.Move)]);
		const bool Curl = Rep.Move == EDuelMove::Ward;
		const bool Lift = Rep.Move == EDuelMove::Flare;
		Entry->SetStringField(TEXT("exercise"), Lift ? TEXT("arm_lift") : Curl ? TEXT("elbow_curl") : TEXT("squat"));
		const FSquatProfile& P = Lift ? LiftProfile : Curl ? CurlProfile : Profile;
		Entry->SetNumberField(TEXT("target_min_deg"), P.TargetMin);
		Entry->SetNumberField(TEXT("target_max_deg"), P.TargetMax);
		Entry->SetNumberField(Lift ? TEXT("estimated_peak_arm_elevation_deg") : TEXT("estimated_peak_flexion_deg"), Rep.PeakFlexion);
		Rows.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Summary->SetArrayField(TEXT("reps"), Rows);
	FString Json;
	FJsonSerializer::Serialize(Summary, TJsonWriterFactory<>::Create(&Json));
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("RehabSessions");
	const FString Path = Dir / (SessionId.ToString() + TEXT(".json"));
	const FString Temp = Path + TEXT(".tmp");
	auto& Files = IFileManager::Get();
	bSaved = Files.MakeDirectory(*Dir, true) && FFileHelper::SaveStringToFile(Json, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		&& Files.Move(*Path, *Temp);
	if (!bSaved)
	{
		UE_LOG(LogRehabDuel, Warning, TEXT("Could not save %s"), *Path);
	}
}

void ARehabDuelController::EndPlay(const EEndPlayReason::Type Reason)
{
	StopOwnedCamera();
	if (ReportProcess.IsValid()) { FPlatformProcess::CloseProc(ReportProcess); ReportProcess.Reset(); }
	if (Bridge)
	{
		Bridge->OnAcceptedPose.RemoveAll(this);
		Bridge->SetPoseOverlayHidden(false);
	}
	if (SessionId.IsValid() && !bSaved)
	{
		if (FinishReason.IsEmpty()) FinishReason = TEXT("Left encounter");
		SaveSummary();
	}
	Super::EndPlay(Reason);
}
