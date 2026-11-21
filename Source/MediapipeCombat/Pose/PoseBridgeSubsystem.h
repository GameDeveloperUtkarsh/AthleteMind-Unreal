#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PoseBridgeSubsystem.generated.h"

class FSocket;

UENUM(BlueprintType)
enum class EPoseBridgeState : uint8
{
	Stopped,
	Waiting,
	Tracked,
	NoPose,
	Stale
};

USTRUCT(BlueprintType)
struct FPoseBridgeLandmark
{
	GENERATED_BODY()

	/** MediaPipe world axes, in meters, with the midpoint of the hips as origin. */
	UPROPERTY(BlueprintReadOnly, Category = "Pose Bridge")
	FVector WorldMeters = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Pose Bridge")
	float Visibility = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Pose Bridge")
	float Presence = 0.0f;
};

USTRUCT(BlueprintType)
struct FPoseBridgeFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Pose Bridge")
	FGuid SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Pose Bridge")
	int64 Sequence = 0;

	/** Monotonic milliseconds in the sender's clock domain. */
	UPROPERTY(BlueprintReadOnly, Category = "Pose Bridge")
	int64 CaptureTimeMs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Pose Bridge")
	bool bTracked = false;

	/** In MediaPipe's 33-point index order. Empty if the sender detected no pose. */
	UPROPERTY(BlueprintReadOnly, Category = "Pose Bridge")
	TArray<FPoseBridgeLandmark> Landmarks;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPoseBridgeFrameEvent, const FPoseBridgeFrame&, Frame);
DECLARE_MULTICAST_DELEGATE_OneParam(FAcceptedPoseSample, const FPoseBridgeFrame&);

UCLASS()
class MEDIAPIPECOMBAT_API UPoseBridgeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Starts a local-only receiver. Starting on the open port again succeeds. */
	UFUNCTION(BlueprintCallable, Category = "Pose Bridge")
	bool StartPoseBridge(int32 Port = 8765);

	UFUNCTION(BlueprintCallable, Category = "Pose Bridge")
	void StopPoseBridge();

	/** A game screen can present its own tracking cues while keeping diagnostics in the log. */
	void SetPoseOverlayHidden(bool bHidden);
	int32 GetListeningPort() const { return ListeningPort; }

	UFUNCTION(BlueprintPure, Category = "Pose Bridge")
	EPoseBridgeState GetTrackingState() const;

	/** Returns a copy of the most recent frame while its pose is fresh. */
	UFUNCTION(BlueprintPure, Category = "Pose Bridge")
	bool GetLatestTrackedFrame(FPoseBridgeFrame& Frame) const;

	/** Computes the unsigned angle A-Joint-C. A straight limb measures 180 degrees. */
	UFUNCTION(BlueprintPure, Category = "Pose Bridge")
	bool TryGetLatestJointAngle(int32 LandmarkA, int32 JointLandmark, int32 LandmarkC,
		float MinimumConfidence, float& AngleDegrees) const;

	/** Fires on the game thread for the newest accepted packet in a tick, including no-pose packets. */
	UPROPERTY(BlueprintAssignable, Category = "Pose Bridge")
	FPoseBridgeFrameEvent OnPoseFrame;

	/** Each accepted sample in order. Exercise consumers must see low confidence between good frames. */
	FAcceptedPoseSample OnAcceptedPose;

private:
	bool TickReceiver(float DeltaTime);

	FSocket* Socket = nullptr;
	FTSTicker::FDelegateHandle TickerHandle;
	FPoseBridgeFrame LatestFrame;
	double LastPacketTime = 0.0;
	int32 ListeningPort = 0;
	bool bDebugDisplay = false;
	bool bOverlayHidden = false;
	EPoseBridgeState LastDebugState = EPoseBridgeState::Stopped;
};
