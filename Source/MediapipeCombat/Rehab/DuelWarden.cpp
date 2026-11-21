#include "Rehab/DuelWarden.h"
#include "Rehab/RehabDuel.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/MaterialBillboardComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"
#include "Misc/PackageName.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FVector WardenAt(60, 115, 134);
const FVector CameraAt(-680,-25,128);
const FVector KeeperAt(-125, -125, 0);
}

ADuelWarden::ADuelWarden()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Stage")));
	Body = CreateDefaultSubobject<USceneComponent>(TEXT("Warden"));
	Body->SetupAttachment(RootComponent);
	Body->SetRelativeLocation(WardenAt);
	Golem = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("StoneGolem"));
	Golem->SetupAttachment(Body);
	Golem->SetRelativeLocation(FVector(0,0,-WardenAt.Z));
	Golem->SetRelativeRotation(FRotator(0,140,0));
	Golem->SetRelativeScale3D(FVector(1.32));
	Golem->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The public checkout uses the UE Third Person assets. Prefer the licensed set when installed.
	bool bUsePacks = true;
	for (const TCHAR* Package : {
		TEXT("/Game/StoneGolem/Mesh/SKM_StoneGolem"), TEXT("/Game/StoneGolem/Animations/AS_StoneGolem_idle1"),
		TEXT("/Game/AthleteMind/Animations/Golem_AS_GroundManaShot"), TEXT("/Game/AthleteMind/Animations/Golem_AS_CastStopSpell"),
		TEXT("/Game/CombatMagicAnims/Demo/Mannequins/Meshes/SKM_Quinn_Simple"),
		TEXT("/Game/CombatMagicAnims/Demo/Mannequins/Anims/Unarmed/MM_Idle"),
		TEXT("/Game/CombatMagicAnims/Animations/AS_MagicStrike"), TEXT("/Game/CombatMagicAnims/Animations/AS_MagicDefend"),
		TEXT("/Game/CombatMagicAnims/Animations/AS_FireballSpell") })
	{
		if (!FPackageName::DoesPackageExist(Package)) { bUsePacks = false; break; }
	}
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Game/AthleteMind/M_RelaySurface.M_RelaySurface"));
	Block = Cube.Object;
	Orb = Sphere.Object;
	Surface = Paint.Object;
	ConstructorHelpers::FObjectFinder<UMaterialInterface> Dusk(TEXT("/Game/AthleteMind/Environment/M_DuskVista.M_DuskVista"));
	auto* Vista = CreateDefaultSubobject<UMaterialBillboardComponent>(TEXT("FloodedCity"));
	Vista->SetupAttachment(RootComponent);
	Vista->SetRelativeLocation(FVector(1760,60,-110));
	Vista->SetHiddenInGame(false);
	Vista->AddElement(Dusk.Object,nullptr,false,830,1475,nullptr);
	ConstructorHelpers::FObjectFinder<USoundWave> GuardCue(TEXT("/Game/AthleteMind/Audio/S_Guard.S_Guard"));
	ConstructorHelpers::FObjectFinder<USoundWave> CounterCue(TEXT("/Game/AthleteMind/Audio/S_Counter.S_Counter"));
	ConstructorHelpers::FObjectFinder<USoundWave> BeaconCue(TEXT("/Game/AthleteMind/Audio/S_Beacon.S_Beacon"));
	GuardSound = GuardCue.Object;
	CounterSound = CounterCue.Object;
	VictorySound = BeaconCue.Object;

	Keeper = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Keeper"));
	Keeper->SetupAttachment(RootComponent);
	ConstructorHelpers::FObjectFinder<USkeletalMesh> Quinn(bUsePacks ? TEXT("/Game/CombatMagicAnims/Demo/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple") : TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"));
	ConstructorHelpers::FObjectFinder<UAnimSequence> Stand(bUsePacks ? TEXT("/Game/CombatMagicAnims/Demo/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle") : TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle"));
	ConstructorHelpers::FObjectFinder<UAnimSequence> Strike(bUsePacks ? TEXT("/Game/CombatMagicAnims/Animations/AS_MagicStrike") : TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01"));
	Keeper->SetSkeletalMeshAsset(Quinn.Object);
	Keeper->SetRelativeLocation(KeeperAt);
	Keeper->SetRelativeRotation(FRotator(0,-44,0));
	Keeper->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Idle = Stand.Object;
	Attack = Strike.Object;
	ConstructorHelpers::FObjectFinder<UAnimSequence> Evade(bUsePacks ? TEXT("/Game/CombatMagicAnims/Animations/AS_MagicDefend") : TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Dash"));
	ConstructorHelpers::FObjectFinder<UAnimSequence> Heavy(bUsePacks ? TEXT("/Game/CombatMagicAnims/Animations/AS_FireballSpell") : TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_ChargedAttack"));
	Dash = Evade.Object;
	HeavyAttack = Heavy.Object;
	if (bUsePacks)
	{
		ConstructorHelpers::FObjectFinder<USkeletalMesh> BossMesh(TEXT("/Game/StoneGolem/Mesh/SKM_StoneGolem"));
		ConstructorHelpers::FObjectFinder<UAnimSequence> BossIdle(TEXT("/Game/StoneGolem/Animations/AS_StoneGolem_idle1"));
		ConstructorHelpers::FObjectFinder<UAnimSequence> BossCast(TEXT("/Game/AthleteMind/Animations/Golem_AS_GroundManaShot"));
		ConstructorHelpers::FObjectFinder<UAnimSequence> BossEvade(TEXT("/Game/AthleteMind/Animations/Golem_AS_CastStopSpell"));
		Golem->SetSkeletalMeshAsset(BossMesh.Object);
		GolemIdle = BossIdle.Object;
		GolemCast = BossCast.Object;
		GolemEvade = BossEvade.Object;
	}
	else
	{
		Golem->SetSkeletalMeshAsset(Quinn.Object);
		GolemIdle = Idle;
		GolemCast = HeavyAttack;
		GolemEvade = Dash;
	}
	Pulse = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ReflectedPulse"));
	Pulse->SetupAttachment(RootComponent);
	Pulse->SetStaticMesh(Orb);
	Pulse->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("EncounterCamera"));
	Camera->SetupAttachment(RootComponent);
	Camera->SetRelativeLocation(CameraAt);
	Camera->SetRelativeRotation(FRotator(-4,2,0));
	Camera->SetConstraintAspectRatio(false);
	Camera->FieldOfView = 57;
	Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
	Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
	Camera->PostProcessSettings.AutoExposureMinBrightness = 1;
	Camera->PostProcessSettings.AutoExposureMaxBrightness = 1;
	auto Light = [&](const TCHAR* Name, FVector At, FLinearColor Color, float Power)
	{
		auto* Lamp = CreateDefaultSubobject<UPointLightComponent>(Name);
		Lamp->SetupAttachment(RootComponent);
		Lamp->SetRelativeLocation(At);
		Lamp->SetLightColor(Color);
		Lamp->SetIntensity(Power);
		Lamp->SetAttenuationRadius(1400);
		Lamp->SetCastShadows(false);
	};
	Light(TEXT("Moonlight"), FVector(-320,-250,360), FLinearColor(0.48,0.72,1), 33000);
	Light(TEXT("BeaconLight"), FVector(-100,260,180), FLinearColor(1,0.46,0.17), 22000);
}

void ADuelWarden::BeginPlay()
{
	Super::BeginPlay();
	if (!Block || !Orb || !Surface) return;
	auto Paint = [&](FLinearColor Tint, float Glow)
	{
		auto* Mat = UMaterialInstanceDynamic::Create(Surface, this);
		Mat->SetVectorParameterValue(TEXT("Tint"), Tint);
		Mat->SetScalarParameterValue(TEXT("Glow"), Glow);
		return Mat;
	};
	// Each palette entry shares one draw group for the architecture or moving opponent.
	auto Group = [&](USceneComponent* Parent, UStaticMesh* Mesh, FLinearColor Tint, float Glow)
	{
		auto* Parts = NewObject<UInstancedStaticMeshComponent>(this);
		Parts->SetupAttachment(Parent);
		Parts->SetStaticMesh(Mesh);
		Parts->SetMaterial(0, Paint(Tint, Glow));
		Parts->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Parts->SetCastShadow(false);
		Parts->RegisterComponent();
		AddInstanceComponent(Parts);
		return Parts;
	};
	auto Place = [](UInstancedStaticMeshComponent* Parts, FVector Center, FVector Size, float Roll = 0.f, float Yaw = 0.f)
	{
		const auto Bounds = Parts->GetStaticMesh()->GetBounds();
		const FVector Scale = Size / (2 * Bounds.BoxExtent);
		const FQuat Rotation = FRotator(0,Yaw,Roll).Quaternion();
		const FVector Offset = Rotation.RotateVector(Scale * Bounds.Origin);
		Parts->AddInstance(FTransform(Rotation, Center-Offset, Scale));
	};
	auto* Sky = Group(RootComponent, Block, FLinearColor(.033,.054,.105), 1.0f);
	Place(Sky, FVector(3000,0,610), FVector(60,6400,4000));
	auto* Near = Group(RootComponent, Block, FLinearColor(.025,.046,.079), .7f);
	auto* Beacon = Group(RootComponent, Orb, FLinearColor(.11,.17,.19), .2f);
	RelayLights = Cast<UMaterialInstanceDynamic>(Beacon->GetMaterial(0));
	Place(Near, FVector(760,12,90), FVector(59,23,280));
	Place(Beacon, FVector(757,12,244), FVector(23,21,42));
	for (int32 I=-3; I<=3; ++I)
	{
		Place(Near,FVector(405,I*124,4),FVector(22,24,68));
		Place(Beacon,FVector(405,I*124,42),FVector(10,12,14));
	}
	auto* Stone = Group(RootComponent, Block, FLinearColor(.045,.068,.09), .34f);
	auto* Inlay = Group(RootComponent, Block, FLinearColor(.12,.52,.49), .9f);
	Place(Stone, FVector(-40,0,-21), FVector(1050,1120,40));
	auto* Bronze = Group(RootComponent, Block, FLinearColor(.32,.21,.11),.36f);
	for (int32 I=0; I<64; ++I)
	{
		const float Angle = I*2*PI/64;
		const FVector Radial(FMath::Cos(Angle),FMath::Sin(Angle),0);
		const float Yaw = FMath::RadiansToDegrees(Angle);
		Place(Bronze,Radial*253,FVector(2,25,1),0,Yaw);
		Place(Inlay,Radial*240,FVector(1,20,1),0,Yaw);
		if (I%4 == 0) Place(Bronze,Radial*265,FVector(17,4,1),0,Yaw);
	}

	PulseLight = Paint(FLinearColor(.15,.9,.8), 3.2f);
	Pulse->SetMaterial(0, PulseLight);
	ResetEncounter();
}

float ADuelWarden::ContactFraction(bool bDodge, EDuelMove Move)
{
	return bDodge || Move == EDuelMove::Ward ? .43f : .58f;
}

void ADuelWarden::SetCameraMotion(bool Enabled)
{
	bCameraMotion = Enabled;
	Camera->SetRelativeLocation(CameraAt);
	Camera->SetFieldOfView(57);
}

void ADuelWarden::ResetEncounter()
{
	Body->SetRelativeLocationAndRotation(WardenAt, FRotator::ZeroRotator);
	Keeper->SetRelativeLocation(KeeperAt);
	Keeper->PlayAnimation(Idle, true);
	Keeper->SetPlayRate(1.f);
	Golem->SetPlayRate(1.f);
	SetCameraMotion(bCameraMotion);
	Pulse->SetVisibility(false);
	Golem->PlayAnimation(GolemIdle, true);
	MotionTime = StrikeDuration;
	bDodging = false;
	SetDodgeReady(false);
	SetActorTickEnabled(false);
}

void ADuelWarden::SetDodgeReady(bool bReady)
{
	if (bReady && PulseLight) PulseLight->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1,.28,.08));
	if (bReady) Pulse->SetRelativeLocation(WardenAt);
	Pulse->SetVisibility(bReady);
}

void ADuelWarden::PlayAction(USkeletalMeshComponent* Mesh, UAnimationAsset* Clip)
{
	if (!Clip) return;
	Mesh->PlayAnimation(Clip, false);
	Mesh->SetPlayRate(Clip->GetPlayLength() / StrikeDuration);
}

void ADuelWarden::PlayDodge()
{
	SetDodgeReady(true);
	bDodging = true;
	if (PulseLight) PulseLight->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1,.28,.08));
	bStagger = false;
	MotionTime = 0;
	Pulse->SetRelativeLocation(WardenAt);
	Pulse->SetRelativeScale3D(FVector(.24f));
	Pulse->SetVisibility(true);
	PlayAction(Keeper, Dash);
	PlayAction(Golem, GolemCast);
	UGameplayStatics::PlaySound2D(this,GuardSound,.50f,.85f);
	SetActorTickEnabled(true);
}

void ADuelWarden::ResolveStrike(EDuelMove Move)
{
	CastMove = Move;
	const bool bCounter = Move == EDuelMove::Charged || Move == EDuelMove::Flare;
	const FLinearColor Tint = Move == EDuelMove::Ward ? FLinearColor(.25,.53,1) :
		Move == EDuelMove::Flare ? FLinearColor(1,.47,.08) : FLinearColor(.15,.9,.8);
	if (PulseLight) PulseLight->SetVectorParameterValue(TEXT("Tint"), Tint);
	UGameplayStatics::PlaySound2D(this, bCounter ? CounterSound : GuardSound, .6f);
	bStagger = bCounter;
	bDodging = false;
	MotionTime = 0;
	Pulse->SetVisibility(true);
	Pulse->SetRelativeScale3D(FVector(.17,.17,.17));
	Pulse->SetRelativeLocation(KeeperAt + FVector(32,20,107));
	PlayAction(Keeper, bCounter ? HeavyAttack : Attack);
	PlayAction(Golem, GolemEvade);
	SetActorTickEnabled(true);
}

void ADuelWarden::SetRelayLit(bool bLit)
{
	Body->SetVisibility(!bLit, true);
	if (RelayLights)
	{
		RelayLights->SetVectorParameterValue(TEXT("Tint"), bLit ? FLinearColor(.25,1,.75) : FLinearColor(.11,.17,.19));
		RelayLights->SetScalarParameterValue(TEXT("Glow"), bLit ? 5.f : .2f);
	}
	if (bLit) UGameplayStatics::PlaySound2D(this, VictorySound, .6f);
}

void ADuelWarden::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	MotionTime += DeltaSeconds;
	const float T = FMath::Clamp(MotionTime/StrikeDuration,0.f,1.f);
	constexpr float Contact = .43f;
	const FVector Hand = KeeperAt + FVector(32,20,107);
	const float Incoming = FMath::Min(1.f,T/Contact);
	const float Outgoing = FMath::Max(0.f,(T-Contact)/(1-Contact));
	const float Impact = FMath::Sin(PI*Outgoing);
	FVector Shot = bDodging ? FMath::Lerp(WardenAt, Hand+FVector(-85,-30,0), FMath::SmoothStep(0.f,.81f,T))
		: FMath::Lerp(Hand,WardenAt,FMath::SmoothStep(.18f,.58f,T));
	if (!bDodging && CastMove == EDuelMove::Ward)
	{
		const float Bounce = T < Contact ? 1.f-Incoming : FMath::SmoothStep(0.f,.77f,Outgoing);
		Shot = FMath::Lerp(Hand+FVector(18,18,0),WardenAt,Bounce);
	}
	else if (!bDodging && CastMove == EDuelMove::Flare)
	{
		Shot.Z += 100.f*FMath::Sin(PI*FMath::SmoothStep(.18f,.58f,T));
	}
	Pulse->SetRelativeLocation(Shot);
	Pulse->SetVisibility(T > .14f && T < .85f);
	Pulse->SetRelativeScale3D(bDodging ? FVector(.29,.85,.12) : FVector(.17f + Impact*.24f));
	const float Sweep = FMath::Sin(PI*Incoming);
	Body->SetRelativeLocation(WardenAt + FVector(bDodging ? -22*Sweep : 10*Impact, bDodging ? 0 : 58*Impact, 0));
	
	const float DashTravel = FMath::Sin(PI * FMath::SmoothStep(.08f,.92f,T));
	Keeper->SetRelativeLocation(KeeperAt + (bDodging ? FVector(-24,-85,0)*DashTravel : FVector(46,34,0)*Sweep));
	// A bounded impulse at projectile contact, evaluated from absolute animation time.
	const float ContactTime = ContactFraction(bDodging, CastMove);
	const float Seconds = (T-ContactTime)*StrikeDuration;
	const float Beat = Seconds >= 0.f && Seconds < .23f ? FMath::Square(1.f-Seconds/.23f) : 0.f;
	if (bCameraMotion)
	{
		const float Kick = Beat*(bStagger ? 4.f : bDodging ? 1.8f : 3.f);
		Camera->SetRelativeLocation(CameraAt + FVector(0, Kick*FMath::Sin(Seconds*95.f), .6f*Kick*FMath::Sin(Seconds*63.f)));
		Camera->SetFieldOfView(57 - (bStagger ? 3 : 1)*Impact);
	}
	if (T >= 1) ResetEncounter();
}
