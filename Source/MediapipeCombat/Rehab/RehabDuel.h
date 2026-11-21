#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Pose/PoseBridgeSubsystem.h"
#include "Rehab/SquatCycle.h"
#include "RehabDuel.generated.h"

class ADuelWarden;

UCLASS()
class ARehabDuelMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ARehabDuelMode();
};

enum class EDuelStep { Intro, Setup, Choose, Move, Resolve, Paused, Rest, Finished };
enum class EDuelMove { Strike, Charged, Ward, Flare };
enum class EDuelGesture { Untracked, None, Lowered, Reach, Select };
struct FScoredRep
{
	EDuelMove Move;
	float PeakFlexion;
};

UCLASS(Config=Game)
class ARehabDuelController : public APlayerController
{
	GENERATED_BODY()
public:
	ARehabDuelController();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PlayerTick(float DeltaSeconds) override;
	virtual void SetupInputComponent() override;

	UFUNCTION(Exec) void DuelGuard();
	UFUNCTION(Exec) void DuelPractice();
	UFUNCTION(Exec) void DuelLive();
	UFUNCTION(Exec) void DuelConnect();
	UFUNCTION(Exec) void DuelCounter();
	UFUNCTION(Exec) void DuelWard();
	UFUNCTION(Exec) void DuelFlare();
	UFUNCTION(Exec) void DuelHandsFree();
	UFUNCTION(Exec) void DuelSide();
	UFUNCTION(Exec) void DuelMotion();
	UFUNCTION(Exec) void DuelContinue();
	UFUNCTION(Exec) void DuelRestart();
	UFUNCTION(Exec) void DuelReport();
	UFUNCTION(Exec) void DuelPause();
	UFUNCTION(Exec) void DuelSkip();
	UFUNCTION(Exec) void DuelFinish();
	UFUNCTION(Exec) void DuelDiscomfort();

private:
	friend class ARehabDuelHUD;
	bool HasKneeSignal() const;
	void ObserveSetupStance(const FPoseBridgeFrame& Frame);
	UPROPERTY(Config) bool bLearnStartingStance = false;
	FSquatProfile KneeSession;
	int64 SetupStart = -1, SetupLast = -1;
	FGuid SetupSender;
	float SetupAnchor = 0, SetupNeutral = -1;
	bool bRightSide = false;
	UPROPERTY(Config) bool bCameraMotion = true;
	bool CoreExposed() const { return (Reps.Num() + PracticeTurns + Skipped) % 2 == 1; }
	int32 DamageFor(EDuelMove Move) const;
	FIntVector MeasuredJoints() const;
	bool ReadJoint(float& Angle) const;
	FString ArmPoseCue(const FPoseBridgeFrame& Frame, float Angle) const;
	FString PoseCorrection;
	FString ReportText;
	bool bReportReady = false;
	FProcHandle ReportProcess;
	void StartReport();
	void PollReport();
	const FSquatProfile& MovementProfile() const;
	TArray<FName> MenuActions() const;
	FName FocusedAction() const;
	void ShiftFocus(int32 Direction);
	void ActivateFocus();
	void PadNext();
	void PadPrevious();
	void PadConfirm();
	void PadPause();
	void ReadMenuGesture(const FPoseBridgeFrame& Frame);
	EDuelGesture HandPose(const FPoseBridgeFrame& Frame, bool bOther = false) const;
	bool TryGesturePause(const FPoseBridgeFrame& Frame, float Angle);
	bool GestureMenu() const;
	int32 TurnLimit() const { return bPractice ? 3 : Profile.PlannedReps; }
	void ResolveResponse();
	void ResolveDodge();
	void StopOwnedCamera();
	UPROPERTY(Config) FSquatProfile Profile;
	UPROPERTY(Config) FSquatProfile CurlProfile;
	UPROPERTY(Config) FSquatProfile LiftProfile;
	UPROPERTY(Config) bool bEnableArmMoves = true;
	UPROPERTY() TObjectPtr<UPoseBridgeSubsystem> Bridge;
	UPROPERTY() TObjectPtr<ADuelWarden> Warden;
	void ReadPose(const FPoseBridgeFrame& Frame);

	EDuelStep Step = EDuelStep::Intro;
	EDuelStep ResumeStep = EDuelStep::Choose;
	FSquatCycle Cycle;
	FGuid SessionId;
	FGuid SenderId;
	int64 LastCapture = -1;
	double LastArrival = 0;
	float LastRawAngle = 0;
	float OlderRawAngle = 0;
	int32 AngleSamples = 0;
	EDuelMove Move = EDuelMove::Strike;
	EDuelStep FocusStep = EDuelStep::Intro;
	FName FocusAction;
	bool bHandsFree = false;
	bool bPadUsed = false;
	bool bPauseArmed = false;
	int64 PauseStart = -1;
	int64 PauseLast = -1;
	double PauseArrival = 0;
	FGuid PauseSender;
	int64 PauseReachStart = -1;
	int64 LastSequence = -1;
	bool bRecoveringSignal = false;
	EDuelStep GestureStep = EDuelStep::Intro;
	EDuelGesture Candidate = EDuelGesture::None;
	bool bGestureArmed = false;
	FGuid GestureSender;
	int64 GestureLast = -1;
	int64 GestureStart = -1;
	double GestureArrival = 0;
	float GestureFill = 0;
	double LastPadInput = -1;
	int32 Wards = 0;
	bool bPractice = false;
	int32 PracticeTurns = 0;
	int32 Hull = 0;
	int32 MaxHull = 0;
	int32 LastDamage = 0;
	bool bEnemyTurn = false;
	int32 Dodges = 0;
	int32 SkippedDodges = 0;
	int32 PreviewPhase = 0;
	float ResolveTime = 0;
	double LastContinueTime = -1;
	FProcHandle CameraProcess;
	int32 Charge = 0;
	int32 Skipped = 0;
	int32 Interruptions = 0;
	float PeakFlexion = 0;
	TArray<FScoredRep> Reps;
	FString Outcome;
	FString FinishReason;
	bool bSaved = false;

	void NewSet(bool bKeyboard);
	void Choose(EDuelMove Choice);
	void Interrupt();
	void ClearMovement();
	void CloseTurn();
	void Finish(const FString& Reason);
	void SaveSummary();
};

UCLASS()
class ARehabDuelHUD : public AHUD
{
	GENERATED_BODY()
public:
	ARehabDuelHUD();
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;
private:
	UPROPERTY() TObjectPtr<class UTexture2D> GuideAtlas;
};
