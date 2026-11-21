#include "Rehab/SquatCycle.h"

bool FSquatProfile::IsValid() const
{
	return MinimumConfidence > 0 && MinimumConfidence <= 1 && NeutralMax >= 0 &&
		NeutralMax < TargetMin && TargetMin < TargetMax && TargetMax <= 180 &&
		NeutralHoldMs > 0 && TargetHoldMs > 0 && DodgeHoldMs > 0 && PlannedReps > 0;
}

bool FSquatCycle::HeldSince(int64 Time, int32 RequiredMs)
{
	if (HoldStart < 0)
	{
		HoldStart = Time;
	}
	return Time - HoldStart >= RequiredMs;
}

void FSquatCycle::Reset()
{
	*this = FSquatCycle();
}

float FSquatCycle::HoldFraction(int64 Time, int32 RequiredMs) const
{
	return HoldStart < 0 ? 0.f : FMath::Clamp(float(Time-HoldStart)/RequiredMs, 0.f, 1.f);
}

bool FSquatCycle::HoldStartingStance(const FSquatProfile& P, float Angle, int64 Time, int32 RequiredMs)
{
	if (Angle > P.NeutralMax)
	{
		HoldStart = -1;
		return false;
	}
	if (HoldStart < 0 || FMath::Abs(Angle-NeutralAnchor) > 5.f)
	{
		NeutralAnchor = Angle;
		HoldStart = Time;
	}
	return HeldSince(Time, RequiredMs);
}

bool FSquatCycle::Observe(const FSquatProfile& P, float Angle, int64 Time)
{
	if (Angle > P.TargetMax)
	{
		Reset();
		bBeyondTarget = true;
		return false;
	}

	if (Phase == ESquatPhase::FindNeutral)
	{
		if (Angle > P.NeutralMax) { HoldStart = -1; return false; }
		if (HeldSince(Time, P.NeutralHoldMs))
		{
			Phase = ESquatPhase::Ready;
			bBeyondTarget = false;
			HoldStart = -1;
		}
		return false;
	}

	if (Phase == ESquatPhase::Returning)
	{
		if (Angle > P.NeutralMax)
		{
			HoldStart = -1;
			return false;
		}
		if (HeldSince(Time, P.NeutralHoldMs))
		{
			Reset();
			return true;
		}
		return false;
	}

	if (Angle < P.TargetMin)
	{
		Phase = ESquatPhase::Ready;
		HoldStart = -1;
	}
	else
	{
		Phase = ESquatPhase::Holding;
		if (HeldSince(Time, P.TargetHoldMs))
		{
			Phase = ESquatPhase::Returning;
			HoldStart = -1;
		}
	}
	return false;
}
