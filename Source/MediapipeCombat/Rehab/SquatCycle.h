#pragma once

#include "CoreMinimal.h"
#include "SquatCycle.generated.h"

/** Developer test settings, expressed as knee flexion (0 degrees near straight). */
USTRUCT()
struct FSquatProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) bool bMeasureRight = false;
	UPROPERTY(EditAnywhere) float MinimumConfidence = 0.5f;
	UPROPERTY(EditAnywhere) float NeutralMax = 20.0f;
	UPROPERTY(EditAnywhere) float TargetMin = 50.0f;
	UPROPERTY(EditAnywhere) float TargetMax = 95.0f;
	UPROPERTY(EditAnywhere) int32 NeutralHoldMs = 200;
	UPROPERTY(EditAnywhere) int32 TargetHoldMs = 250;
	UPROPERTY(EditAnywhere) int32 DodgeHoldMs = 1000;
	UPROPERTY(EditAnywhere) int32 PlannedReps = 3;

	bool IsValid() const;
};

enum class ESquatPhase { FindNeutral, Ready, Holding, Returning };

struct FSquatCycle
{
	ESquatPhase Phase = ESquatPhase::FindNeutral;
	bool bBeyondTarget = false;
	// One call per fresh, confidence-qualified sample. Returns true once on completion.
	bool Observe(const FSquatProfile& Profile, float Flexion, int64 CaptureMs);
	bool HoldStartingStance(const FSquatProfile& Profile, float Flexion, int64 CaptureMs, int32 RequiredMs);
	float HoldFraction(int64 Time, int32 RequiredMs) const;
	void Reset();
	void BreakHold() { HoldStart = -1; }

private:
	int64 HoldStart = -1;
	float NeutralAnchor = 0.0f;
	bool HeldSince(int64 CaptureMs, int32 RequiredMs);
};
