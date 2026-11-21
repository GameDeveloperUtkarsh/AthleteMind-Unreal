#include "Pose/PoseBridgeSubsystem.h"

#include "Common/UdpSocketBuilder.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "IPAddress.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

DEFINE_LOG_CATEGORY_STATIC(LogPoseBridge, Log, All);

namespace
{
	constexpr int32 PoseBridgeVersion = 1;
	constexpr int32 PoseLandmarkCount = 33;
	constexpr int32 MaxPosePacketBytes = 8192;
	constexpr int32 MaxPosePacketsPerTick = 32;
	constexpr double PoseTimeoutSeconds = 0.5;
	constexpr double MaxExactJsonInteger = 9007199254740991.0;

	bool ReadPoseInteger(const FJsonObject& Object, const TCHAR* FieldName, int64& Result)
	{
		const TSharedPtr<FJsonValue> Field = Object.TryGetField(FieldName);
		double Number = 0.0;
		if (!Field || Field->Type != EJson::Number || !Field->TryGetNumber(Number)
			|| !FMath::IsFinite(Number) || Number < 0.0 || Number > MaxExactJsonInteger
			|| FMath::FloorToDouble(Number) != Number)
		{
			return false;
		}
		Result = static_cast<int64>(Number);
		return true;
	}

	bool ReadPoseLandmark(const FJsonValue& Value, FPoseBridgeLandmark& Landmark)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Value.TryGetArray(Values) || Values->Num() != 5)
		{
			return false;
		}

		double Components[5];
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Components); ++Index)
		{
			const FJsonValue& Component = *(*Values)[Index];
			if (Component.Type != EJson::Number || !Component.TryGetNumber(Components[Index])
				|| !FMath::IsFinite(Components[Index]))
			{
				return false;
			}
		}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			// Bound corrupt input well outside the range of a hip-centered human pose.
			if (FMath::Abs(Components[Axis]) > 10.0)
			{
				return false;
			}
		}
		if (Components[3] < 0.0 || Components[3] > 1.0 || Components[4] < 0.0 || Components[4] > 1.0)
		{
			return false;
		}

		Landmark.WorldMeters = FVector(Components[0], Components[1], Components[2]);
		Landmark.Visibility = static_cast<float>(Components[3]);
		Landmark.Presence = static_cast<float>(Components[4]);
		return true;
	}

	bool ParsePosePacket(const uint8* Data, int32 Size, FPoseBridgeFrame& Frame)
	{
		if (Size <= 0 || Size > MaxPosePacketBytes)
		{
			return false;
		}

		const FUTF8ToTCHAR Converted(reinterpret_cast<const UTF8CHAR*>(Data), Size);
		const FString JsonText(Converted.Length(), Converted.Get());
		TSharedPtr<FJsonObject> Object;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Object) || !Object)
		{
			return false;
		}

		int64 Version = 0;
		FString Session;
		const TArray<TSharedPtr<FJsonValue>>* Landmarks = nullptr;
		if (!ReadPoseInteger(*Object, TEXT("version"), Version) || Version != PoseBridgeVersion
			|| !Object->TryGetStringField(TEXT("session"), Session)
			|| !FGuid::ParseExact(Session, EGuidFormats::DigitsWithHyphens, Frame.SessionId)
			|| !Frame.SessionId.IsValid()
			|| !ReadPoseInteger(*Object, TEXT("seq"), Frame.Sequence)
			|| !ReadPoseInteger(*Object, TEXT("capture_ms"), Frame.CaptureTimeMs)
			|| !Object->HasTypedField<EJson::Boolean>(TEXT("tracked"))
			|| !Object->TryGetBoolField(TEXT("tracked"), Frame.bTracked)
			|| !Object->TryGetArrayField(TEXT("world"), Landmarks)
			|| Landmarks->Num() != (Frame.bTracked ? PoseLandmarkCount : 0))
		{
			return false;
		}

		Frame.Landmarks.Reserve(Landmarks->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Landmarks)
		{
			if (!ReadPoseLandmark(*Value, Frame.Landmarks.AddDefaulted_GetRef()))
			{
				return false;
			}
		}
		return true;
	}
}

void UPoseBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (IsRunningCommandlet() || GetGameInstance()->IsDedicatedServerInstance()
		|| FParse::Param(FCommandLine::Get(), TEXT("NoPoseBridge")))
	{
		return;
	}
	bDebugDisplay = FParse::Param(FCommandLine::Get(), TEXT("PoseBridgeDebug"))
		|| (WITH_EDITOR && !FParse::Param(FCommandLine::Get(), TEXT("PoseBridge")));
	int32 Port = 8765;
	FParse::Value(FCommandLine::Get(), TEXT("PoseBridgePort="), Port);
	if (!StartPoseBridge(Port) && bDebugDisplay && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red,
			FString::Printf(TEXT("Pose receiver could not start on port %d. See LogPoseBridge in the Output Log."), Port));
	}
}

void UPoseBridgeSubsystem::Deinitialize()
{
	StopPoseBridge();
	Super::Deinitialize();
}

bool UPoseBridgeSubsystem::StartPoseBridge(int32 Port)
{
	if (Port < 1 || Port > 65535)
	{
		UE_LOG(LogPoseBridge, Warning, TEXT("Invalid pose bridge port %d."), Port);
		return false;
	}
	if (Socket && Port == ListeningPort)
	{
		return true;
	}

	FSocket* NewSocket = FUdpSocketBuilder(TEXT("AthleteMind Pose Bridge"))
		.AsNonBlocking()
		.BoundToAddress(FIPv4Address::InternalLoopback)
		.BoundToPort(static_cast<uint16>(Port))
		.WithReceiveBufferSize(64 * 1024)
		.Build();
	if (!NewSocket)
	{
		UE_LOG(LogPoseBridge, Warning, TEXT("Could not bind the pose bridge to 127.0.0.1:%d."), Port);
		return false;
	}

	StopPoseBridge();
	Socket = NewSocket;
	ListeningPort = Port;
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickReceiver));
	UE_LOG(LogPoseBridge, Display, TEXT("Listening for pose frames on 127.0.0.1:%d."), Port);
	return true;
}

void UPoseBridgeSubsystem::StopPoseBridge()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (Socket)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	if (bDebugDisplay && GEngine)
	{
		GEngine->RemoveOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()));
	}
	LatestFrame = FPoseBridgeFrame();
	LastPacketTime = 0.0;
	ListeningPort = 0;
	LastDebugState = EPoseBridgeState::Stopped;
}

void UPoseBridgeSubsystem::SetPoseOverlayHidden(bool bHidden)
{
	bOverlayHidden = bHidden;
	if (bHidden && GEngine)
	{
		GEngine->RemoveOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()));
	}
}

EPoseBridgeState UPoseBridgeSubsystem::GetTrackingState() const
{
	if (!Socket)
	{
		return EPoseBridgeState::Stopped;
	}
	if (!LatestFrame.SessionId.IsValid())
	{
		return EPoseBridgeState::Waiting;
	}
	if (FPlatformTime::Seconds() - LastPacketTime >= PoseTimeoutSeconds)
	{
		return EPoseBridgeState::Stale;
	}
	return LatestFrame.bTracked ? EPoseBridgeState::Tracked : EPoseBridgeState::NoPose;
}

bool UPoseBridgeSubsystem::GetLatestTrackedFrame(FPoseBridgeFrame& Frame) const
{
	const bool bHasPose = GetTrackingState() == EPoseBridgeState::Tracked;
	Frame = bHasPose ? LatestFrame : FPoseBridgeFrame();
	return bHasPose;
}

bool UPoseBridgeSubsystem::TryGetLatestJointAngle(int32 LandmarkA, int32 JointLandmark, int32 LandmarkC,
	float MinimumConfidence, float& AngleDegrees) const
{
	AngleDegrees = 0.0f;
	if (GetTrackingState() != EPoseBridgeState::Tracked || !FMath::IsFinite(MinimumConfidence)
		|| MinimumConfidence < 0.0f || MinimumConfidence > 1.0f)
	{
		return false;
	}
	for (const int32 Index : { LandmarkA, JointLandmark, LandmarkC })
	{
		if (!LatestFrame.Landmarks.IsValidIndex(Index)
			|| LatestFrame.Landmarks[Index].Visibility < MinimumConfidence
			|| LatestFrame.Landmarks[Index].Presence < MinimumConfidence)
		{
			return false;
		}
	}

	const FVector A = (LatestFrame.Landmarks[LandmarkA].WorldMeters
		- LatestFrame.Landmarks[JointLandmark].WorldMeters).GetSafeNormal();
	const FVector C = (LatestFrame.Landmarks[LandmarkC].WorldMeters
		- LatestFrame.Landmarks[JointLandmark].WorldMeters).GetSafeNormal();
	if (A.IsZero() || C.IsZero())
	{
		return false;
	}
	const double Cosine = FMath::Clamp(FVector::DotProduct(A, C), -1.0, 1.0);
	AngleDegrees = static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(Cosine)));
	return true;
}

bool UPoseBridgeSubsystem::TickReceiver(float)
{
	const double Now = FPlatformTime::Seconds();
	bool bReceivedFrame = false;
	// A zero-byte datagram is readable too. Drain it so later frames can be reached.
	if (Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::Zero()))
	{
		const TSharedRef<FInternetAddr> Sender = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
		uint8 Buffer[MaxPosePacketBytes + 1];
		for (int32 Index = 0; Index < MaxPosePacketsPerTick; ++Index)
		{
			int32 BytesRead = 0;
			if (!Socket->RecvFrom(Buffer, sizeof(Buffer), BytesRead, *Sender))
			{
				break;
			}
			FPoseBridgeFrame Candidate;
			if (!ParsePosePacket(Buffer, BytesRead, Candidate))
			{
				continue;
			}
			if (LatestFrame.SessionId == Candidate.SessionId)
			{
				if (Candidate.Sequence <= LatestFrame.Sequence || Candidate.CaptureTimeMs <= LatestFrame.CaptureTimeMs)
				{
					continue;
				}
			}
			else if (LatestFrame.SessionId.IsValid() && Now - LastPacketTime < PoseTimeoutSeconds)
			{
				continue;
			}
			LatestFrame = MoveTemp(Candidate);
			LastPacketTime = Now;
			bReceivedFrame = true;
			if (OnAcceptedPose.IsBound())
			{
				const auto ActiveTicker = TickerHandle.Pin();
				const FPoseBridgeFrame Sample = LatestFrame;
				OnAcceptedPose.Broadcast(Sample);
				if (ActiveTicker != TickerHandle.Pin()) return false;
			}
		}
	}

	if (bDebugDisplay)
	{
		const EPoseBridgeState State = GetTrackingState();
		const FString StateName = StaticEnum<EPoseBridgeState>()->GetNameStringByValue(static_cast<int64>(State));
		float LeftAngle = 0.0f;
		float RightAngle = 0.0f;
		const bool bLeftValid = TryGetLatestJointAngle(23, 25, 27, 0.5f, LeftAngle);
		const bool bRightValid = TryGetLatestJointAngle(24, 26, 28, 0.5f, RightAngle);
		const FString Left = bLeftValid ? FString::Printf(TEXT("%.1f deg"), LeftAngle) : TEXT("--");
		const FString Right = bRightValid ? FString::Printf(TEXT("%.1f deg"), RightAngle) : TEXT("--");
		const FString Message = FString::Printf(TEXT("Pose %s  |  Knee L %s  R %s"), *StateName, *Left, *Right);
		if (GEngine && !bOverlayHidden)
		{
			GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.3f, FColor::Cyan, Message);
		}
		if (State != LastDebugState)
		{
			UE_LOG(LogPoseBridge, Display, TEXT("%s"), *Message);
			LastDebugState = State;
		}
	}

	if (bReceivedFrame && OnPoseFrame.IsBound())
	{
		// A Blueprint handler can stop or restart the receiver while handling the event.
		const FPoseBridgeFrame FrameForEvent = LatestFrame;
		OnPoseFrame.Broadcast(FrameForEvent);
	}
	return true;
}
