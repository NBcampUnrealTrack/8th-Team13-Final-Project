// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/FF_FestivalCharacter.h"

#include "Camera/FF_FestivalPlayerController.h"

#include "Camera/CameraComponent.h"
#include "Camera/FF_CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "FF_FFZSCollision.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "TimerManager.h"

AFF_FestivalCharacter::AFF_FestivalCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);
	// Pawn ObjectType은 유지해 지면·기존 월드 충돌을 보존하고 시설 전용 채널만 선택 참여한다.
	GetCapsuleComponent()->SetCollisionResponseToChannel(FFZSCollision::FacilityPlayerObjectChannel, ECR_Block);

	// 반지름 34cm 는 Crowd 에이전트와 같은 값이다. 연동 가이드가 NPC 두 명의 비겹침
	// 중심거리를 68cm 로 잡고 시설 간격을 그 기준으로 보정했으므로, 플레이어만 다른 몸집이면
	// 대기열 슬롯이나 통로에서 혼자 끼거나 혼자 통과하는 상황이 생긴다.

	// 컨트롤러 회전은 스프링암만 따라간다. 몸은 이동 방향을 본다.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = true;
		Movement->RotationRate = FRotator(0.f, 540.f, 0.f);
		Movement->MaxWalkSpeed = WalkSpeed;
		Movement->MinAnalogWalkSpeed = 20.f;
		Movement->BrakingDecelerationWalking = 2000.f;
		Movement->JumpZVelocity = 420.f;
		Movement->AirControl = 0.2f;
	}

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 320.f;
	CameraBoom->bUsePawnControlRotation = true;
	// 관찰 카메라와 달리 여기서는 충돌 검사를 켠다. 3인칭 시점이라 벽이 카메라와 몸 사이에
	// 끼면 화면이 막히는데, 부스가 빽빽한 축제장에서는 그 상황이 자주 나온다.
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->SocketOffset = FVector(0.f, 40.f, 60.f);

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void AFF_FestivalCharacter::BeginPlay()
{
	Super::BeginPlay();
	// 기존 캐릭터 Blueprint에 저장된 응답이 새 채널을 Ignore로 덮어써도 플레이 시 복구한다.
	GetCapsuleComponent()->SetCollisionResponseToChannel(FFZSCollision::FacilityPlayerObjectChannel, ECR_Block);
	ApplyMovementSpeed();

	GetWorldTimerManager().SetTimer(
		SafeSampleTimer, this, &AFF_FestivalCharacter::SampleSafeLocation, SafeSampleIntervalSeconds, true);
}

void AFF_FestivalCharacter::SampleSafeLocation()
{
	// 관찰 시점에서는 캐릭터가 조종되지 않고 서 있다. 그때는 기록할 필요가 없다.
	if (!GetController())
	{
		return;
	}

	// 공중이나 수영 중의 위치는 기록하지 않는다. 기록은 전부 서 있을 수 있는 땅이어야 한다.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround())
	{
		return;
	}

	const FVector Location = GetActorLocation();
	if (!SafeLocations.IsEmpty()
		&& FVector::DistSquared(SafeLocations.Last(), Location) < FMath::Square(SafeSampleSpacingCm))
	{
		return;
	}

	SafeLocations.Add(Location);
	if (SafeLocations.Num() > MaxSafeSamples)
	{
		SafeLocations.RemoveAt(0);
	}
}

bool AFF_FestivalCharacter::RestoreToLastSafeLocation()
{
	if (SafeLocations.IsEmpty())
	{
		// 위험 지형에서 시작했거나 기록 없이 곧장 떨어진 경우다. 옮길 곳을 알 수 없다.
		if (!bWarnedNoSafeLocation)
		{
			bWarnedNoSafeLocation = true;
			UE_LOG(LogFestaFiestaCamera, Warning,
				TEXT("[캐릭터] 되돌아갈 땅 기록이 없다. 시작 위치가 위험 지형 안이 아닌지 확인할 것."));
		}
		return false;
	}

	// 가장 최근 기록은 낭떠러지 끝이다. 걸어온 길을 따라 RestoreBackoffCm 만큼 거슬러 올라간다.
	int32 Index = SafeLocations.Num() - 1;
	float Travelled = 0.f;
	while (Index > 0 && Travelled < RestoreBackoffCm)
	{
		Travelled += FVector::Dist2D(SafeLocations[Index], SafeLocations[Index - 1]);
		--Index;
	}

	const FVector Destination = SafeLocations[Index] + FVector(0.f, 0.f, RestoreLiftCm);
	if (!TeleportTo(Destination, GetActorRotation()))
	{
		UE_LOG(LogFestaFiestaCamera, Warning,
			TEXT("[캐릭터] 안전한 땅 (%s) 으로 옮기지 못했다. 그 자리가 다른 물체와 겹쳐 있을 수 있다."),
			*Destination.ToCompactString());
		return false;
	}

	// 도착한 지점보다 뒤의 기록은 버린다. 안 그러면 다음에 떨어질 때 이미 지나온 끝 쪽이 또 뽑힌다.
	SafeLocations.SetNum(Index + 1);
	bWarnedNoSafeLocation = false;

	// 낙하 속도가 남으면 도착하자마자 튕기거나 바닥을 뚫는다.
	// 수영 상태로 남지 않도록 이동 모드도 걷기로 되돌린다.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Walking);
	}

	UE_LOG(LogFestaFiestaCamera, Log,
		TEXT("[캐릭터] 안전한 땅 (%s) 으로 되돌렸다."), *Destination.ToCompactString());
	return true;
}

void AFF_FestivalCharacter::FellOutOfWorld(const UDamageType& DamageType)
{
	// 파괴하면 플레이어가 조종할 몸이 사라진다. 돌아갈 땅이 있으면 살려 둔다.
	if (RestoreToLastSafeLocation())
	{
		return;
	}

	Super::FellOutOfWorld(DamageType);
}

void AFF_FestivalCharacter::ApplyMovementSpeed()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
	}
}

void AFF_FestivalCharacter::AddMappingContext()
{
	if (!CharacterMappingContext)
	{
		UE_LOG(LogFestaFiestaCamera, Warning,
			TEXT("[캐릭터] CharacterMappingContext 가 비어 있어 조작이 동작하지 않는다. ")
			TEXT("캐릭터 블루프린트에서 IMC 와 InputAction 을 지정할 것."));
		return;
	}

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (!LocalPlayer)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
	{
		InputSubsystem->AddMappingContext(CharacterMappingContext, CharacterMappingPriority);
	}
}

void AFF_FestivalCharacter::RemoveMappingContext()
{
	if (!CharacterMappingContext)
	{
		return;
	}

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (!LocalPlayer)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
	{
		InputSubsystem->RemoveMappingContext(CharacterMappingContext);
	}
}

void AFF_FestivalCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();

	// 빙의가 들어온 시점이다. 여기서 컨텍스트를 붙여야 캐릭터를 조종하는 동안에만
	// 이 키들이 살아 있다. 컨트롤러 BeginPlay 에서 붙이면 관찰 시점에서도 계속 켜져 있다.
	AddMappingContext();
}

void AFF_FestivalCharacter::UnPossessed()
{
	// 관찰 시점으로 돌아갈 때 캐릭터 키를 남겨두면 WASD 가 양쪽에 다 먹는다.
	RemoveMappingContext();

	// 달리던 중에 빙의가 빠지면 상태가 남는다. 토글이라 다시 조종할 때 저절로 달리고 있으면 놀란다.
	SetSprinting(false);

	Super::UnPossessed();
}

void AFF_FestivalCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogFestaFiestaCamera, Warning,
			TEXT("[캐릭터] EnhancedInputComponent 가 아니다. 프로젝트 입력 설정을 확인할 것."));
		return;
	}

	if (MoveAction)
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFF_FestivalCharacter::HandleMove);
	}
	if (LookAction)
	{
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFF_FestivalCharacter::HandleLook);
	}
	if (JumpAction)
	{
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &AFF_FestivalCharacter::HandleJumpStarted);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &AFF_FestivalCharacter::HandleJumpCompleted);
	}
	if (SprintAction)
	{
		// 토글이든 누르는 동안이든 둘 다 묶는다. 어느 쪽으로 동작할지는 핸들러가 bToggleSprint 로 가른다.
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &AFF_FestivalCharacter::HandleSprintStarted);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &AFF_FestivalCharacter::HandleSprintCompleted);
	}
	if (ShiftLockAction)
	{
		// 누를 때마다 켜고 끈다. 누르는 동안만 켜지게 하면 오래 조준할 때 손가락이 아프다.
		EnhancedInput->BindAction(ShiftLockAction, ETriggerEvent::Started, this, &AFF_FestivalCharacter::HandleShiftLockToggled);
	}
}

void AFF_FestivalCharacter::HandleMove(const FInputActionValue& Value)
{
	const AController* OwningController = GetController();
	if (!OwningController)
	{
		return;
	}

	const FVector2D Input = Value.Get<FVector2D>();
	if (Input.IsNearlyZero())
	{
		return;
	}

	// 카메라가 보는 방향 기준으로 움직인다. Pitch 를 빼야 위를 보고 있을 때 땅으로 파고들지 않는다.
	const FRotator YawOnly(0.f, OwningController->GetControlRotation().Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);

	AddMovementInput(Forward, Input.Y);
	AddMovementInput(Right, Input.X);
}

void AFF_FestivalCharacter::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	AddControllerYawInput(Input.X * LookSensitivity);
	AddControllerPitchInput(Input.Y * LookSensitivity);
}

void AFF_FestivalCharacter::HandleShiftLockToggled(const FInputActionValue& Value)
{
	SetShiftLock(!bShiftLock);
}

void AFF_FestivalCharacter::SetShiftLock(const bool bEnabled)
{
	if (bShiftLock == bEnabled)
	{
		return;
	}

	bShiftLock = bEnabled;

	/* 둘을 함께 바꾼다.

	   컨트롤러 회전 추종만 켜고 이동 방향 정렬을 남겨두면, 한쪽은 카메라를
	   따라 돌리려 하고 다른 쪽은 걷는 방향으로 돌리려 해서 캐릭터가 떨린다.
	*/
	bUseControllerRotationYaw = bEnabled;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = !bEnabled;
	}

	// 커서를 켜고 끄는 건 컨트롤러가 맡는다. 입력 모드는 한 곳에서만 정해야
	// 서로 다른 값을 걸어 덮어쓰는 일이 없다.
	if (AFF_FestivalPlayerController* FestivalController =
		Cast<AFF_FestivalPlayerController>(GetController()))
	{
		FestivalController->RefreshViewInputMode();
	}

	UE_LOG(LogFestaFiestaCamera, Verbose,
		TEXT("[캐릭터] 시야 고정 %s"), bEnabled ? TEXT("켬") : TEXT("끔"));
}

void AFF_FestivalCharacter::HandleJumpStarted(const FInputActionValue& Value)
{
	Jump();
}

void AFF_FestivalCharacter::HandleJumpCompleted(const FInputActionValue& Value)
{
	StopJumping();
}

void AFF_FestivalCharacter::HandleSprintStarted(const FInputActionValue& Value)
{
	// 토글이면 누를 때마다 뒤집는다. 아니면 누르는 순간 켠다.
	if (bToggleSprint)
	{
		ToggleSprint();
		return;
	}
	SetSprinting(true);
}

void AFF_FestivalCharacter::HandleSprintCompleted(const FInputActionValue& Value)
{
	// 토글이면 손을 떼도 그대로 달린다. 다음에 누를 때 끈다.
	if (!bToggleSprint)
	{
		SetSprinting(false);
	}
}

void AFF_FestivalCharacter::SetSprinting(const bool bEnabled)
{
	if (bSprinting == bEnabled)
	{
		return;
	}

	bSprinting = bEnabled;
	ApplyMovementSpeed();

	UE_LOG(LogFestaFiestaCamera, Verbose,
		TEXT("[캐릭터] 달리기 %s"), bEnabled ? TEXT("켬") : TEXT("끔"));
}
