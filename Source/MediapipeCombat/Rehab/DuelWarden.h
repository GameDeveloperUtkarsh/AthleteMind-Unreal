#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DuelWarden.generated.h"

enum class EDuelMove;
class UAnimationAsset;
class UCameraComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USoundWave;

UCLASS()
class ADuelWarden : public AActor
{
	GENERATED_BODY()
public:
	ADuelWarden();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	void ResolveStrike(EDuelMove Move);
	void PlayDodge();
	void SetDodgeReady(bool bReady);
	void ResetEncounter();
	void SetCameraMotion(bool Enabled);
	void SetRelayLit(bool bLit);
	static float ContactFraction(bool bDodge, EDuelMove Move);
	static constexpr float StrikeDuration = 1.6f;

private:
	UPROPERTY() TObjectPtr<USceneComponent> Body;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Golem;
	UPROPERTY() TObjectPtr<UAnimationAsset> GolemIdle;
	UPROPERTY() TObjectPtr<UAnimationAsset> GolemCast;
	UPROPERTY() TObjectPtr<UAnimationAsset> GolemEvade;
	void PlayAction(USkeletalMeshComponent* Mesh, UAnimationAsset* Clip);
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Keeper;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Pulse;
	UPROPERTY() TObjectPtr<UAnimationAsset> Idle;
	UPROPERTY() TObjectPtr<UAnimationAsset> Attack;
	UPROPERTY() TObjectPtr<UAnimationAsset> Dash;
	UPROPERTY() TObjectPtr<UAnimationAsset> HeavyAttack;
	UPROPERTY() TObjectPtr<UStaticMesh> Block;
	UPROPERTY() TObjectPtr<UStaticMesh> Orb;
	UPROPERTY() TObjectPtr<UMaterialInterface> Surface;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RelayLights;
	UPROPERTY() TObjectPtr<USoundWave> GuardSound;
	UPROPERTY() TObjectPtr<USoundWave> CounterSound;
	UPROPERTY() TObjectPtr<USoundWave> VictorySound;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PulseLight;
	EDuelMove CastMove{};
	float MotionTime = StrikeDuration;
	bool bStagger = false;
	bool bDodging = false;
	bool bCameraMotion = true;
};
