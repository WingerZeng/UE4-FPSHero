// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSHeroCharacter.h"
#include <algorithm>

#include "AIController.h"
#include "FPSHeroGameMode.h"
#include "FPSHeroGrenade.h"
#include "FPSHeroWeapon.h"
#include "FPSHeroGrenade.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/UnrealMathUtility.h"
#include "Net/UnrealNetwork.h"
#include "FPSHeroGameStateBase.h"
#include "FPSHeroHUD.h"
class AAIController;
DEFINE_LOG_CATEGORY_STATIC(LogFPChar, Warning, All);

void AFPSHeroCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFPSHeroCharacter, CurrentHealth);
	DOREPLIFETIME(AFPSHeroCharacter, MaxHealth);
	DOREPLIFETIME(AFPSHeroCharacter, Attack);
	DOREPLIFETIME(AFPSHeroCharacter, Defence);
	DOREPLIFETIME(AFPSHeroCharacter, bIsTurning);
	DOREPLIFETIME(AFPSHeroCharacter, Weapons);
	DOREPLIFETIME(AFPSHeroCharacter, CurrentWeaponSlot);
	DOREPLIFETIME(AFPSHeroCharacter, bInitialized);
	DOREPLIFETIME(AFPSHeroCharacter, TeamID);
	DOREPLIFETIME(AFPSHeroCharacter, bIsJumping);
}

void AFPSHeroCharacter::GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	OutLocation = FirstPersonCameraComponent->GetComponentLocation();
	OutRotation = GetControlRotation();
}

FVector AFPSHeroCharacter::GetPawnViewLocation() const
{
	return GetFirstPersonCameraComponent()->GetComponentLocation();
}

void AFPSHeroCharacter::OnRep_Weapons()
{
	OnWeaponUpdate();
}

EWeaponSlot AFPSHeroCharacter::GetWeaponSlot(AFPSHeroWeaponBase* Weapon)
{
	return Weapon->GetSlot();
}

EWeaponSlot AFPSHeroCharacter::GetNextWeaponSlot(EWeaponSlot WeaponSlot)
{
	return static_cast<EWeaponSlot>((static_cast<uint8>(WeaponSlot) + 1) % static_cast<uint8>(EWeaponSlot::MAX_SLOT));
}

EWeaponSlot AFPSHeroCharacter::FindValidSlotFromSlot(EWeaponSlot WeaponSlot)
{
	EWeaponSlot InitSlot = WeaponSlot;
	do {
		if (GetWeapon(WeaponSlot) && GetWeapon(WeaponSlot)->IsValidLowLevel())
			return WeaponSlot;
		WeaponSlot = GetNextWeaponSlot(WeaponSlot);
	} while (WeaponSlot != InitSlot);
	return WeaponSlot;
}

EWeaponSlot AFPSHeroCharacter::FindValidSlotFromNextSlot(EWeaponSlot WeaponSlot)
{
	EWeaponSlot InitSlot = GetNextWeaponSlot(WeaponSlot);
	WeaponSlot = InitSlot;
	do {
		if (GetWeapon(WeaponSlot) && GetWeapon(WeaponSlot)->IsValidLowLevel())
			return WeaponSlot;
		WeaponSlot = GetNextWeaponSlot(WeaponSlot);
	} while (WeaponSlot != InitSlot);
	return WeaponSlot;
}

void AFPSHeroCharacter::OnHealthUpdate()
{
	if(IsLocallyControlled())
	{
		AFPSHeroHUD* HUD = GetFPSHeroHUD();
		if(HUD)
		{
			HUD->CharacterUpdate();
		}
	}
	if (CurrentHealth <= 0)
	{
		if(!bIsDead)
		{
			bIsDead = true;
			OnDie();	
		}	
	}
	OnHealthChanged.Broadcast(this,GetHealth());
}

void AFPSHeroCharacter::OnWeaponUpdate()
{
	UpdateAnimationClass();
	for (const auto& Weapon : Weapons) {
		if(Weapon)
			Weapon->DetachFromCharacter();
	}
	if (!GetCurrentWeapon()) {
		return;
	}
	FName WeaponSocketName;
	GetCurrentWeapon()->AttachToCharacter(GetCurrentMesh(), (GetDisplayViewMode() == EViewMode::FIRST_PERSON ? FPSWeaponSocketName : TPSWeaponSocketName));
	if(IsLocallyControlled())
	{
		if(GetFPSHeroHUD())
			GetFPSHeroHUD()->CharacterUpdate();
	}
}

void AFPSHeroCharacter::Jump()
{
	bIsJumping = true;
	Super::Jump();
	//JumpServer();
}

void AFPSHeroCharacter::JumpServer_Implementation()
{
	bIsJumping = true;
	Super::Jump();
}

void AFPSHeroCharacter::OnDie()
{
	if(GetLocalRole() == ENetRole::ROLE_Authority)
	{
		DieMulticaset(LastDamageImpulse, LastDamageLocation, bIsLastDamagePoint);
		AFPSHeroGameMode* GameMode = Cast<AFPSHeroGameMode>(GetWorld()->GetAuthGameMode());
		if (GameMode)
			GameMode->CharacterDie(this, LastDamageInstigator);
	}
}

void AFPSHeroCharacter::OnRep_TeamID()
{
	OnTeamChanged();
}

void AFPSHeroCharacter::DieMulticaset_Implementation(FVector DamageImpulse, FVector DamageLocation, bool bIsDamagePoint)
{
	if(IsLocallyControlled())
		DisableInput(Cast<APlayerController>(GetController()));
	if(bIsFiring)
		EndFire(EFireEndReason::DEAD);
	SetViewMode(EViewMode::DEAD_VIEW);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetCollisionResponseToChannel(TRACECHANNEL_WEAPON, ECollisionResponse::ECR_Ignore);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if(bIsDamagePoint)
		GetMesh()->AddImpulseAtLocation(DamageImpulse, DamageLocation);
	else
		GetMesh()->AddImpulse(DamageImpulse);
	OnDead.Broadcast(this);
}

bool AFPSHeroCharacter::FindWeapon(AFPSHeroWeaponBase* const Weapon, EWeaponSlot& WeaponSlot)
{
	for (int i = 0; i<int(EWeaponSlot::MAX_SLOT); i++) {
		if (GetWeapon(EWeaponSlot(i)) == Weapon) {
			WeaponSlot = EWeaponSlot(i);
			return true;
		}
	}
	return false;
}

void AFPSHeroCharacter::Initialize()
{
	SetViewMode(ViewMode);
	OnWeaponUpdate();
	if(GetFPSHeroHUD())
		GetFPSHeroHUD()->CharacterUpdate();
}

void AFPSHeroCharacter::OnRep_CurrentWeaponSlot()
{
	OnWeaponUpdate();
}

void AFPSHeroCharacter::OnRep_CurrentHealth()
{
	OnHealthUpdate();
}

void AFPSHeroCharacter::OnRep_Initialzed()
{
	Initialize();
}

EWeaponSlot AFPSHeroCharacter::GetWeaponTypeSlot(TSubclassOf<AFPSHeroWeaponBase> WeaponType)
{
	return WeaponType->GetDefaultObject<AFPSHeroWeaponBase>()->GetSlot();
}

AFPSHeroCharacter::AFPSHeroCharacter()
{
	bReplicates = true;
	SetReplicatingMovement(true);

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Create a CameraComponent	
	FirstPersonCameraHolder = CreateDefaultSubobject<USceneComponent>(TEXT("FirstPersonCameraHolder"));
	FirstPersonCameraHolder->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraHolder->SetRelativeLocation(FVector(0.0f, 0.0f, 64.f)); // Position the camera

	DeadViewCameraHolder = CreateDefaultSubobject<USceneComponent>(TEXT("DeadViewCameraHolder"));

	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->SetupAttachment(FirstPersonCameraHolder);

	ThirdPersonSpringArmComp = CreateDefaultSubobject<USpringArmComponent>(TEXT("ThirdPersonSpringArm"));
	ThirdPersonSpringArmComp->bUsePawnControlRotation = false;
	ThirdPersonSpringArmComp->SetupAttachment(GetCapsuleComponent());

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeRotation(FRotator(1.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-0.5f, -4.4f, -155.7f));

	GetMesh()->SetCollisionProfileName("CharacterMesh");

	FPSWeaponSocketName = "FPSGripPoint";
	TPSWeaponSocketName = "TPSGripPoint";
	DefaultViewMode = EViewMode::FIRST_PERSON;
	DefaultWeaponSlot = EWeaponSlot::RIFLE_SLOT;
	for (int i = 0; i<int(EWeaponSlot::MAX_SLOT); i++)
		Weapons.Push(nullptr);
	bShouldInitAnim = false;

	MaxHealth = 100.f;
	DefaultDefence = 0.0f;
	DefaultAttack = 0.0f;

	GetCapsuleComponent()->SetCollisionResponseToChannel(TRACECHANNEL_WEAPON, ECollisionResponse::ECR_Ignore);
	bInitialized = false;

	CurrentWeaponSlot = EWeaponSlot::MAX_SLOT;

	this->PrimaryActorTick.bCanEverTick = true;
}

AFPSHeroWeaponBase* AFPSHeroCharacter::GetCurrentWeapon()
{
	return GetWeapon(CurrentWeaponSlot);
}

AFPSHeroWeaponBase* AFPSHeroCharacter::GetWeapon(EWeaponSlot Slot)
{
	if (int(Slot) >= Weapons.Num() || int(Slot) < 0)
		return nullptr;
	return Weapons[int(Slot)];
}

void AFPSHeroCharacter::SetWeapon(EWeaponSlot Slot, AFPSHeroWeaponBase* Weapon)
{
	if (int(Slot) >= Weapons.Num() || int(Slot) < 0)
		return;
	Weapons[int(Slot)] = Weapon;
}

void AFPSHeroCharacter::GripWeapon_Implementation(TSubclassOf<class AFPSHeroWeaponBase> GrappedWeaponType)
{
	if (GrappedWeaponType && GetLocalRole() == ROLE_Authority) {
		EWeaponSlot ThisSlot = GetWeaponTypeSlot(GrappedWeaponType);
		if (ThisSlot == EWeaponSlot::MAX_SLOT)
			return;
		ThrowWeapon(ThisSlot);
		FActorSpawnParameters paras;
		paras.Owner = this;
		paras.Instigator = this;
		paras.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SetWeapon(ThisSlot, GetWorld()->SpawnActor<AFPSHeroWeaponBase>(GrappedWeaponType, FTransform(), paras));
		GetWeapon(ThisSlot)->SetOwnerCharacter(this, ThisSlot);
		GetWeapon(ThisSlot)->SetOwner(this);

		if (ThisSlot == CurrentWeaponSlot)
		{
			SwitchToWeaponSlot(ThisSlot);
		}
		else {
			GetWeapon(ThisSlot)->SetWeaponActive(false);
		}
	}
}

void AFPSHeroCharacter::ThrowWeapon_Implementation(EWeaponSlot WeaponSlot, bool bShouldChangeWeaponAuto)
{
	if (GetLocalRole() == ROLE_Authority && GetWeapon(WeaponSlot)) {
		GetWeapon(WeaponSlot)->Throw();
		SetWeapon(WeaponSlot, nullptr);
		if (WeaponSlot == CurrentWeaponSlot && bShouldChangeWeaponAuto)
			SwitchToWeaponSlot(FindValidSlotFromSlot(CurrentWeaponSlot));
	}
}

void AFPSHeroCharacter::SwitchToWeaponSlot(EWeaponSlot WeaponSlot)
{
	if (GetWeapon(WeaponSlot) && GetLocalRole() == ROLE_Authority) {
		if (bIsFiring)
			EndFireServer(EFireEndReason::SWAP_WEAPON);
		if (GetCurrentWeapon()) {
			GetCurrentWeapon()->SetWeaponActive(false);
		}
		CurrentWeaponSlot = WeaponSlot;
		GetCurrentWeapon()->SetWeaponActive(true);
		
		OnWeaponUpdate();
	}
}

void AFPSHeroCharacter::SetViewModeServer_Implementation(EViewMode NewViewMode)
{
	//#TODO0 Do Some Check
	if (bIsFiring)
		EndFireServer(EFireEndReason::SWITCH_VIEW_MODE);
	// 更新所有主机的视角
	SetViewModeNetMulticast(NewViewMode);
}

void AFPSHeroCharacter::SetViewModeNetMulticast_Implementation(EViewMode NewViewMode)
{
	SetViewMode(NewViewMode);
}

void AFPSHeroCharacter::SetViewMode(EViewMode NewViewMode)
{
	ViewMode = NewViewMode;
	if (ViewMode == EViewMode::FIRST_PERSON) {
		if (IsLocallyControlled()) {
			GetMesh()->SetVisibility(false);
			Mesh1P->SetVisibility(true);
		}
		ThirdPersonSpringArmComp->bUsePawnControlRotation = false;
		ThirdPersonSpringArmComp->SetRelativeRotation(FRotator());
		FirstPersonCameraComponent->ResetRelativeTransform();
		FirstPersonCameraComponent->AttachToComponent(FirstPersonCameraHolder, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		FirstPersonCameraComponent->bUsePawnControlRotation = true;
		FirstPersonCameraComponent->SetUsingAbsoluteRotation(false);
	}
	else if (ViewMode == EViewMode::THIRD_PERSON) {
		if (IsLocallyControlled()) {
			Mesh1P->SetVisibility(false);
			GetMesh()->SetVisibility(true);
		}
		FirstPersonCameraComponent->bUsePawnControlRotation = false;
		FirstPersonCameraComponent->ResetRelativeTransform();
		ThirdPersonSpringArmComp->SetRelativeRotation(FRotator());
		FirstPersonCameraComponent->AttachToComponent(ThirdPersonSpringArmComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		ThirdPersonSpringArmComp->bUsePawnControlRotation = true;
		FirstPersonCameraComponent->SetUsingAbsoluteRotation(false);
	}
	else if(ViewMode == EViewMode::DEAD_VIEW)
	{
		ViewMode = EViewMode::THIRD_PERSON;
		if (IsLocallyControlled()) {
			Mesh1P->SetVisibility(false);
			GetMesh()->SetVisibility(true);
		}
		FirstPersonCameraComponent->bUsePawnControlRotation = false;
		FirstPersonCameraComponent->ResetRelativeTransform();
		ThirdPersonSpringArmComp->bUsePawnControlRotation = false;
		ThirdPersonSpringArmComp->SetRelativeRotation(FRotator());
		FirstPersonCameraComponent->AttachToComponent(DeadViewCameraHolder, FAttachmentTransformRules::SnapToTargetIncludingScale);
	}
	OnWeaponUpdate();
}

EViewMode AFPSHeroCharacter::GetViewMode()
{
	return ViewMode;
}

EViewMode AFPSHeroCharacter::GetDisplayViewMode()
{
	if(IsLocallyControlled())
		return ViewMode;
	else
		return EViewMode::THIRD_PERSON;
}

void AFPSHeroCharacter::SwitchViewMode()
{
	if (ViewMode == EViewMode::FIRST_PERSON) {
		SetViewModeServer(EViewMode::THIRD_PERSON);
	}
	else if (ViewMode == EViewMode::THIRD_PERSON) {
		SetViewModeServer(EViewMode::FIRST_PERSON);
	}
}

void AFPSHeroCharacter::UpdateAnimationClass()
{
	bShouldInitAnim = true;
	if (GetCurrentMesh()->IsPostEvaluatingAnimation())
		return;
	AFPSHeroWeaponBase* CurWeapon = GetCurrentWeapon();
	TSubclassOf<UAnimInstance> NewAnim;
	if (CurWeapon) {
		NewAnim = CurWeapon->GetAnimClass(GetDisplayViewMode());
	}
	
	if (!NewAnim) {
		NewAnim = GetDefaultAnimClass(GetDisplayViewMode());
	}
	if (NewAnim) {
		GetCurrentMesh()->SetAnimClass(NewAnim);
	}
	bShouldInitAnim = false;
}

UAnimInstance* AFPSHeroCharacter::GetAnimInstance()
{
	if (GetDisplayViewMode() == EViewMode::FIRST_PERSON) {
		return Mesh1P->GetAnimInstance();
	}
	else if (GetDisplayViewMode() == EViewMode::THIRD_PERSON) {
		return GetMesh()->GetAnimInstance();
	}
	return Mesh1P->GetAnimInstance();
}

void AFPSHeroCharacter::PlayMontage(UAnimMontage* Montage)
{
	if (Montage != NULL)
	{
		// Get the animation object for the arms mesh
		UAnimInstance* AnimInstance = GetAnimInstance();
		if (AnimInstance != NULL)
		{
			AnimInstance->Montage_Play(Montage);
		}
	}
}

USkeletalMeshComponent* AFPSHeroCharacter::GetCurrentMesh()
{
	if (GetDisplayViewMode() == EViewMode::FIRST_PERSON) {
		return Mesh1P;
	}
	else if (GetDisplayViewMode() == EViewMode::THIRD_PERSON) {
		return GetMesh();
	}
	return Mesh1P;
}

bool AFPSHeroCharacter::IsTurning()
{
	return bIsTurning;
}


bool AFPSHeroCharacter::IsFiring()
{
	return bIsFiring;
}

void AFPSHeroCharacter::Destroyed()
{
	while(Weapons.Num())
	{
		AFPSHeroWeaponBase* Weapon = Weapons.Last();
		Weapons.Pop();
		if(Weapon&&Weapon->IsValidLowLevel())
			Weapon->Destroy();
	}
	Super::Destroyed();
}

TSubclassOf<UAnimInstance> AFPSHeroCharacter::GetDefaultAnimClass(EViewMode AnimViewMode)
{
	if (AnimViewMode == EViewMode::FIRST_PERSON) {
		return DefaultFPSAnimClass;
	}
	else
		return DefaultTPSAnimClass;
}

float AFPSHeroCharacter::GetMaxHealth()
{
	return MaxHealth; 
}

void AFPSHeroCharacter::OnRep_MaxHealth()
{
	OnHealthUpdate();	
}

float AFPSHeroCharacter::GetHealth()
{
	return CurrentHealth;
}

void AFPSHeroCharacter::SetHealth(float Health)
{
	if (GetLocalRole() == ROLE_Authority) {
		CurrentHealth = FMath::Clamp(Health, 0.0f, MaxHealth);
		OnHealthUpdate();
	}
}

float AFPSHeroCharacter::GetAttack()
{
	return Attack;
}

void AFPSHeroCharacter::SetAttack(float NewAttack)
{
	if(GetLocalRole() == ROLE_Authority)
		Attack = FMath::Max(0.0f, NewAttack);
}

float AFPSHeroCharacter::GetDefence()
{
	return Defence;
}

void AFPSHeroCharacter::SetDefence(float NewDefence)
{
	if (GetLocalRole() == ROLE_Authority)
		Defence = FMath::Max(0.0f, NewDefence);
}

FRotator AFPSHeroCharacter::GetViewRotation() const
{
	if(GetLocalRole() == ENetRole::ROLE_Authority || IsLocallyControlled())
		return Super::GetViewRotation();
	return CharacterViewRotation;
}

void AFPSHeroCharacter::SetTeam(int Team)
{
	if(GetLocalRole() == ENetRole::ROLE_Authority)
	{
		TeamID = Team;
		OnRep_TeamID();
	}
}

int AFPSHeroCharacter::GetTeam() const
{
	return TeamID;
}

ETEAM AFPSHeroCharacter::GetETeam() const
{
	return static_cast<ETEAM>(TeamID);
}

FRotator AFPSHeroCharacter::GetCharacterViewRotation() const
{
	return GetViewRotation();
}

void AFPSHeroCharacter::OnControllerChanged_Implementation()
{
	if (GetLocalRole() == ROLE_Authority) {
		const auto FPSPlayerState = Cast<AFPSHeroPlayerStateBase> (GetPlayerState());
		if(FPSPlayerState)
			SetTeam(FPSPlayerState->GetTeam());
		CurrentHealth = MaxHealth;
		Attack = DefaultAttack;
		Defence = DefaultDefence;
		CurrentWeaponSlot = DefaultWeaponSlot;

		for (const auto& WeaponType : DefaultWeaponTypes) {
			GripWeapon(WeaponType);
		}
		SwitchToWeaponSlot(FindValidSlotFromSlot(DefaultWeaponSlot));
		bInitialized = true;
		Initialize();
	}
}

AFPSHeroHUD* AFPSHeroCharacter::GetFPSHeroHUD()
{
	APlayerController* FPSController =  Cast<APlayerController>(GetController());
	if(FPSController)
	{
		return Cast<AFPSHeroHUD>(FPSController->GetHUD());
	}
	return nullptr;
}

FVector AFPSHeroCharacter::GetBotAimLocation()
{
	if(!GetMesh() || !GetMesh()->IsValidLowLevel())
	{
		return GetActorLocation();
	}
	return GetMesh()->GetSocketLocation(BotAimSocketName);
}

bool AFPSHeroCharacter::IsDead()
{
	return bIsDead;
}

void AFPSHeroCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();
	OnControllerChanged();
	OnTeamChanged();
}

void AFPSHeroCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Update Camera Rotation on Server
	if(GetLocalRole() == ROLE_Authority)
	{
		USceneComponent* CameraRotateComponent;
		if(ViewMode == EViewMode::FIRST_PERSON)
			CameraRotateComponent = FirstPersonCameraComponent;
		else
			CameraRotateComponent = ThirdPersonSpringArmComp;
		CameraRotateComponent->SetWorldRotation(GetViewRotation());
		SetCharacterViewRotation(GetViewRotation());
	}

	if (bShouldInitAnim)
		UpdateAnimationClass();

	if (GetLocalRole() == ROLE_Authority || IsLocallyControlled()) {
		float DeltaYaw = UKismetMathLibrary::NormalizedDeltaRotator(GetViewRotation(), GetActorRotation()).Yaw;
		if (abs(DeltaYaw) > TurnThresholdStartAngle || bIsTurning) {
			if (abs(DeltaYaw) < TurnThresholdStopAngle) {
				bIsTurning = false;
			}
			else {
				float TurnYaw = UKismetMathLibrary::FInterpTo(0, DeltaYaw, DeltaSeconds, TurnRate);
				FRotator TurnRotation(0, TurnYaw, 0);
				SetActorRotation(TurnRotation + GetActorRotation());
				bIsTurning = true;
			}
		}
	}
}

float AFPSHeroCharacter::InternalTakePointDamage(float Damage, FPointDamageEvent const& PointDamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	Super::InternalTakePointDamage(Damage, PointDamageEvent, EventInstigator, DamageCauser);
	if (GetLocalRole() == ROLE_Authority) {
		//关闭友军伤害
		if(AFPSHeroGameMode* GameMode = Cast<AFPSHeroGameMode>(GetWorld()->GetAuthGameMode()))
		{
			if(!GameMode->IsGameStarted())
				return 0;
		}
		AFPSHeroCharacter* DamagorCharacter = Cast<AFPSHeroCharacter>(EventInstigator->GetCharacter());
		if(DamagorCharacter)
		{
			if(DamagorCharacter->GetTeam() == GetTeam() && DamagorCharacter->GetTeam() != ETEAM_NONE)
				return 0;
		}
		//#TODO1 EventInstigator作为复制变量，需要在该玩家HUD中显示伤害
		LastDamageInstigator = EventInstigator;
		//记录冲量
		FVector ShotDir = FVector(PointDamageEvent.ShotDirection);
		ShotDir.Normalize();
		LastDamageImpulse = ShotDir * PointDamageEvent.DamageTypeClass->GetDefaultObject<UDamageType>()->DamageImpulse;
		LastDamageLocation = PointDamageEvent.HitInfo.Location;
		bIsLastDamagePoint = true;
		float ActualDamage = CalDamage(Damage, DamagorCharacter); 
		SetHealth(GetHealth() - ActualDamage);
		return ActualDamage;
	}
	return 0;
}

float AFPSHeroCharacter::InternalTakeRadialDamage(float Damage, FRadialDamageEvent const& RadialDamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	Super::InternalTakeRadialDamage(Damage, RadialDamageEvent, EventInstigator, DamageCauser);
	if (GetLocalRole() == ROLE_Authority) {
		//关闭友军伤害
		AFPSHeroCharacter* DamagorCharacter = Cast<AFPSHeroCharacter>(EventInstigator->GetCharacter());
		if(DamagorCharacter)
		{
			if(DamagorCharacter->GetTeam() == GetTeam() && DamagorCharacter->GetTeam() != ETEAM_NONE)
				return 0;
		}
		//#TODO1 EventInstigator作为复制变量，需要在该玩家HUD中显示伤害
		LastDamageInstigator = EventInstigator;
		//记录冲量
		FVector DamageDir = this->GetActorLocation() - RadialDamageEvent.Origin;
		DamageDir.Normalize();
		LastDamageImpulse = DamageDir * RadialDamageEvent.DamageTypeClass->GetDefaultObject<UDamageType>()->DamageImpulse;
		bIsLastDamagePoint = false;
		float ActualDamage = CalDamage(Damage, DamagorCharacter); 
		SetHealth(GetHealth() - ActualDamage);
		return ActualDamage;
	}
	return 0;
}

float AFPSHeroCharacter::CalDamage_Implementation(float Damage,  AFPSHeroCharacter* DamageInstigator)
{
	Damage = FMath::Pow(DefencePowBase, GetDefence()) * FMath::Pow(AttackPowBase, DamageInstigator->GetAttack()) * Damage;
	return Damage;
}

void AFPSHeroCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// set up gameplay key bindings
	check(PlayerInputComponent);
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Bind fire event
	PlayerInputComponent->BindAction("Fire", IE_Pressed, this, &AFPSHeroCharacter::OnFireButtonPress);
	PlayerInputComponent->BindAction("Fire", IE_Released, this, &AFPSHeroCharacter::OnFireButtonRelease);
	PlayerInputComponent->BindAction("SwitchFireMode", IE_Pressed, this, &AFPSHeroCharacter::SwitchFireMode);

	// Bind movement events
	PlayerInputComponent->BindAxis("MoveForward", this, &AFPSHeroCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AFPSHeroCharacter::MoveRight);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &AFPSHeroCharacter::Jump);

	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &AFPSHeroCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AFPSHeroCharacter::LookUpAtRate);

	PlayerInputComponent->BindAction("SwitchViewMode", IE_Pressed, this, &AFPSHeroCharacter::SwitchViewMode);
	PlayerInputComponent->BindAction("Weapon1", IE_Pressed, this, &AFPSHeroCharacter::SwitchToWeaponSlot1);
	PlayerInputComponent->BindAction("Weapon2", IE_Pressed, this, &AFPSHeroCharacter::SwitchToWeaponSlot2);
	PlayerInputComponent->BindAction("Weapon3", IE_Pressed, this, &AFPSHeroCharacter::SwitchToWeaponSlot3);
}

void AFPSHeroCharacter::OnFire()
{
	if (bIsFiring)
		return;
	bIsFiring = true;
	if (GetCurrentWeapon()) {
		GetCurrentWeapon()->Fire();
	}
}

void AFPSHeroCharacter::EndFire(EFireEndReason Reason)
{
	if (!bIsFiring)
		return;
	bIsFiring = false;
	if (GetCurrentWeapon()) {
		GetCurrentWeapon()->EndFire(Reason);
	}
}

void AFPSHeroCharacter::OnFireButtonPress()
{
	if (IsLocallyControlled()) {
		OnFire();
		OnFireServer();
	}
}

void AFPSHeroCharacter::OnFireButtonRelease()
{
	if (IsLocallyControlled()) {
		EndFire(EFireEndReason::MOUSE_REALEASE);
		EndFireServer(EFireEndReason::MOUSE_REALEASE);
	}
}

void AFPSHeroCharacter::OnFireServer_Implementation()
{
	OnFireMulticast();
}

void AFPSHeroCharacter::EndFireServer_Implementation(EFireEndReason Reason /*= EFireEndReason::MOUSE_REALEASE*/)
{
	EndFireMulticast(Reason);
}

void AFPSHeroCharacter::OnFireMulticast_Implementation()
{
	if(!IsLocallyControlled())
		OnFire();
}

void AFPSHeroCharacter::EndFireMulticast_Implementation(EFireEndReason Reason /*= EFireEndReason::MOUSE_REALEASE*/)
{
	if (!IsLocallyControlled() || Reason != EFireEndReason::MOUSE_REALEASE)
		EndFire(Reason);
}

void AFPSHeroCharacter::SwitchFireMode_Implementation()
{
	if (GetCurrentWeapon() && GetLocalRole() == ROLE_Authority) {
		GetCurrentWeapon()->SwitchFireMode();
		EndFireServer(EFireEndReason::SWITCH_FIRE_MODE);
	}
}

void AFPSHeroCharacter::SwitchToWeaponSlot1_Implementation()
{
	SwitchToWeaponSlot(EWeaponSlot::RIFLE_SLOT);
}

void AFPSHeroCharacter::SwitchToWeaponSlot2_Implementation()
{
	SwitchToWeaponSlot(EWeaponSlot::LIGHTWEAPON_SLOT);
}

void AFPSHeroCharacter::RemoveWeapon(EWeaponSlot WeaponSlot)
{
	SetWeapon(WeaponSlot, nullptr);
	if (CurrentWeaponSlot == WeaponSlot) {
		if (bIsFiring)
			EndFireServer(EFireEndReason::REMOVE_WEAPON);
		WeaponSlot = FindValidSlotFromNextSlot(WeaponSlot);
		SwitchToWeaponSlot(WeaponSlot);
	}
}

void AFPSHeroCharacter::SetCharacterViewRotation_Implementation(FRotator Rotator)
{
	if(GetLocalRole() != ENetRole::ROLE_Authority && !IsLocallyControlled())
		CharacterViewRotation = Rotator;
}

int AFPSHeroCharacter::GetKillAwardMoney()
{
	return KillAwardMoney;
}

void AFPSHeroCharacter::SwitchToWeaponSlot3_Implementation()
{
	SwitchToWeaponSlot(EWeaponSlot::GRENADE_SLOT);
}

void AFPSHeroCharacter::MoveForward(float Value)
{
	GetCharacterMovement()->bOrientRotationToMovement = Value > 0;
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(UKismetMathLibrary::GetForwardVector(FRotator(0, FirstPersonCameraComponent->GetComponentRotation().Yaw, 0)), Value);
	}
}

void AFPSHeroCharacter::MoveRight(float Value)
{
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(UKismetMathLibrary::GetRightVector(FRotator(0, FirstPersonCameraComponent->GetComponentRotation().Yaw, 0)), Value);
	}
}

void AFPSHeroCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AFPSHeroCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}