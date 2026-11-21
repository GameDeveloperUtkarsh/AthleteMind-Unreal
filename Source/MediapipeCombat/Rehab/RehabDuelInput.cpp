#include "Rehab/RehabDuel.h"

TArray<FName> ARehabDuelController::MenuActions() const
{
	TArray<FName> Actions;
	switch (Step)
	{
	case EDuelStep::Intro: return { "DuelPractice", "DuelLive", "DuelMotion" };
	case EDuelStep::Setup:
		if (HasKneeSignal()) Actions.Append({ "DuelHandsFree", "DuelContinue" });
		else if (Bridge && Bridge->GetListeningPort() && !CameraProcess.IsValid() &&
			Bridge->GetTrackingState() != EPoseBridgeState::NoPose && Bridge->GetTrackingState() != EPoseBridgeState::Tracked)
			Actions.Add("DuelConnect");
		Actions.Add("DuelSide");
		Actions.Add("DuelPractice");
		return Actions;
	case EDuelStep::Choose:
		Actions.Add("DuelGuard");
		if (Charge > 0) Actions.Add("DuelCounter");
		if (bEnableArmMoves && CurlProfile.IsValid()) Actions.Add("DuelWard");
		if (bEnableArmMoves && LiftProfile.IsValid()) Actions.Add("DuelFlare");
		break;
	case EDuelStep::Move:
		if (bPractice) Actions.Add("DuelContinue");
		break;
	case EDuelStep::Rest:
	case EDuelStep::Paused: Actions.Add("DuelContinue"); break;
	case EDuelStep::Finished:
		Actions.Add("DuelRestart");
		if (!bPractice && !bReportReady && !ReportProcess.IsValid()) Actions.Add("DuelReport");
		return Actions;
	default: break;
	}
	if (Step != EDuelStep::Resolve && Step != EDuelStep::Paused) Actions.Add("DuelPause");
	if (Step == EDuelStep::Move || (Step == EDuelStep::Paused && ResumeStep == EDuelStep::Move)) Actions.Add("DuelSkip");
	if (Step == EDuelStep::Paused || Step == EDuelStep::Resolve) Actions.Add("DuelFinish");
	if (!bPractice) Actions.Add("DuelDiscomfort");
	if (Step == EDuelStep::Paused) Actions.Add("DuelMotion");
	return Actions;
}

FName ARehabDuelController::FocusedAction() const
{
	const auto Actions = MenuActions();
	if (Step == FocusStep && Actions.Contains(FocusAction)) return FocusAction;
	// The animation has no default action. A second confirm must never end the set.
	return Step == EDuelStep::Resolve || (Step == EDuelStep::Move && !bPractice) ? NAME_None : Actions[0];
}

void ARehabDuelController::ShiftFocus(int32 Direction)
{
	if (Step == EDuelStep::Choose) ClearMovement();
	const auto Actions = MenuActions();
	int32 Index = Actions.IndexOfByKey(FocusedAction());
	if (Index == INDEX_NONE && Direction < 0) Index = 0;
	FocusStep = Step;
	FocusAction = Actions[(Index + Direction + Actions.Num()) % Actions.Num()];
	UE_LOG(LogTemp, Verbose, TEXT("Duel focus %s"), *FocusAction.ToString());
}

void ARehabDuelController::ActivateFocus()
{
	// The allowlist is also used by the HUD and excludes unavailable techniques.
	const FName Command = FocusedAction();
	if (UFunction* Action = FindFunction(Command))
		ProcessEvent(Action, nullptr);
	FocusAction = NAME_None;
}

void ARehabDuelController::PadNext() { bPadUsed = true; ShiftFocus(1); }
void ARehabDuelController::PadPrevious() { bPadUsed = true; ShiftFocus(-1); }
void ARehabDuelController::PadConfirm()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastPadInput < .15) return;
	LastPadInput = Now;
	bPadUsed = true;
	ActivateFocus();
}
void ARehabDuelController::PadPause()
{
	bPadUsed = true;
	if (Step == EDuelStep::Paused) DuelContinue();
	else DuelPause();
}

bool ARehabDuelController::GestureMenu() const
{
	if (Step == EDuelStep::Setup) return HasKneeSignal();
	return bHandsFree && (Step == EDuelStep::Choose || Step == EDuelStep::Rest || Step == EDuelStep::Paused);
}

void ARehabDuelController::DuelHandsFree()
{
	if (Step != EDuelStep::Setup || !HasKneeSignal()) return;
	NewSet(false);
	bHandsFree = Step == EDuelStep::Choose;
}

EDuelGesture ARehabDuelController::HandPose(const FPoseBridgeFrame& Frame, bool bOther) const
{
	const int32 Side = bRightSide != bOther ? 1 : 0;
	if (!Frame.bTracked || Frame.Landmarks.Num() != 33) return EDuelGesture::Untracked;
	for (const int32 I : {11+Side, 15+Side, 23+Side})
	{
		const auto& L = Frame.Landmarks[I];
		if (FMath::Min(L.Visibility, L.Presence) < Profile.MinimumConfidence) return EDuelGesture::Untracked;
	}
	const FVector Shoulder = Frame.Landmarks[11+Side].WorldMeters;
	const FVector Hip = Frame.Landmarks[23+Side].WorldMeters;
	const FVector Wrist = Frame.Landmarks[15+Side].WorldMeters;
	const double Torso = FVector::Distance(Shoulder, Hip);
	if (Torso < .12 || Torso > 1) return EDuelGesture::Untracked;
	const FVector Hand = (Wrist-Shoulder)/Torso;
	if (Hand.Y > .7) return EDuelGesture::Lowered;
	if (Hand.Size() < .5) return EDuelGesture::Select;
	if (Hand.Y >= -.25 && Hand.Y <= .42 && FMath::Square(Hand.X)+FMath::Square(Hand.Z) > .65*.65)
		return EDuelGesture::Reach;
	return EDuelGesture::None;
}

bool ARehabDuelController::TryGesturePause(const FPoseBridgeFrame& Frame, float Angle)
{
	const double Now = FPlatformTime::Seconds();
	const int64 T = Frame.CaptureTimeMs;
	const EDuelGesture Other = HandPose(Frame, true);
	const bool AtStart = Angle <= MovementProfile().NeutralMax && Cycle.Phase != ESquatPhase::Returning;
	if (!AtStart || Other == EDuelGesture::Untracked || PauseSender != Frame.SessionId || T <= PauseLast ||
		T-PauseLast > 350 || Now-PauseArrival > .35)
	{
		bPauseArmed = false;
		PauseStart = PauseReachStart = -1;
	}
	PauseSender = Frame.SessionId;
	PauseLast = T;
	PauseArrival = Now;
	if (!AtStart) return false;
	if (Other == EDuelGesture::Reach)
	{
		if (PauseReachStart < 0) PauseReachStart = T;
		if (T-PauseReachStart >= 350) bPauseArmed = true;
	}
	else if (!bPauseArmed) PauseReachStart = -1;
	// The second pose must follow the reach promptly. Folding arms during a squat is common.
	if (Other == EDuelGesture::Lowered || (bPauseArmed && PauseStart < 0 && Other != EDuelGesture::Reach && T-PauseReachStart > 2000))
	{
		bPauseArmed = false;
		PauseReachStart = -1;
	}
	if (Other == EDuelGesture::Reach && bPauseArmed) PauseReachStart = T;
	if (!bPauseArmed || Other != EDuelGesture::Select) { PauseStart = -1; return false; }
	if (PauseStart < 0)
	{
		PauseStart = T;
		Cycle.Reset();
		PeakFlexion = 0;
	}
	// Give a deliberate pause priority, including during the shorter dodge hold.
	if (T-PauseStart < 1250) return true;
	DuelPause();
	UE_LOG(LogTemp, Display, TEXT("Hands-free pause"));
	return true;
}

void ARehabDuelController::ReadMenuGesture(const FPoseBridgeFrame& Frame)
{
	const double Now = FPlatformTime::Seconds();
	const int64 T = Frame.CaptureTimeMs;
	const EDuelGesture Pose = HandPose(Frame);

	if (Step != GestureStep || GestureSender != Frame.SessionId || T <= GestureLast ||
		T-GestureLast > 350 || Now-GestureArrival > .35 || Pose == EDuelGesture::Untracked)
	{
		bGestureArmed = false;
		Candidate = EDuelGesture::None;
		GestureFill = 0;
	}
	GestureStep = Step;
	GestureSender = Frame.SessionId;
	GestureLast = T;
	GestureArrival = Now;
	if (Pose != Candidate)
	{
		Candidate = Pose;
		GestureStart = T;
	}
	if (Pose == EDuelGesture::Lowered)
	{
		GestureFill = 0;
		if (T-GestureStart >= 150) bGestureArmed = true;
		return;
	}
	if (!bGestureArmed || Pose == EDuelGesture::None) { GestureFill = 0; return; }
	GestureFill = FMath::Clamp(float(T-GestureStart)/850.f, 0.f, 1.f);
	if (GestureFill < 1) return;
	bGestureArmed = false;
	GestureFill = 0;
	bPadUsed = false;
	if (Pose == EDuelGesture::Reach) ShiftFocus(1);
	else ActivateFocus();
}
