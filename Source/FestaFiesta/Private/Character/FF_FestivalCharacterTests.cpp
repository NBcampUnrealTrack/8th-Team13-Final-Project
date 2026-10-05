// Copyright Epic Games, Inc. All Rights Reserved.

// 역할: 플레이어만 시설에 부딪히고 일반 캐릭터는 통과하는지 실제 이동으로 검증한다. 달리기 토글도 본다.

#include "Character/FF_FestivalCharacter.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FF_FFZSCollision.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFF_FestivalCharacterFacilityCollisionTest,
	"FestaFiesta.Character.FacilityCollision.PlayerCapsuleSweepAndBlueprintRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFF_FestivalCharacterFacilityCollisionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game))
	{
		Fixture.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = Fixture.GetTestWorld();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Transient physics world"), World)
		|| !TestNotNull(TEXT("Engine cube with authored simple collision"), Cube))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFF_FestivalCharacter* Player = World->SpawnActor<AFF_FestivalCharacter>(
		AFF_FestivalCharacter::StaticClass(), FVector(-250.0, -150.0, 100.0),
		FRotator::ZeroRotator, SpawnParameters);
	ACharacter* OtherCharacter = World->SpawnActor<ACharacter>(
		ACharacter::StaticClass(), FVector(-250.0, 150.0, 100.0),
		FRotator::ZeroRotator, SpawnParameters);
	AActor* FacilityBodyOwner = World->SpawnActor<AActor>(
		AActor::StaticClass(), FVector(0.0, 0.0, 100.0),
		FRotator::ZeroRotator, SpawnParameters);
	if (!TestNotNull(TEXT("Actual festival player"), Player)
		|| !TestNotNull(TEXT("Unmodified ordinary character"), OtherCharacter)
		|| !TestNotNull(TEXT("Facility collision fixture"), FacilityBodyOwner))
	{
		return false;
	}

	// The plugin tests cover real facility lifecycle changes. This independent body
	// isolates the game's opt-in without modifying any facility asset or NPC class.
	UStaticMeshComponent* FacilityBody = NewObject<UStaticMeshComponent>(FacilityBodyOwner);
	FacilityBodyOwner->SetRootComponent(FacilityBody);
	FacilityBodyOwner->AddInstanceComponent(FacilityBody);
	FacilityBody->SetMobility(EComponentMobility::Movable);
	FacilityBody->SetStaticMesh(Cube);
	FacilityBody->SetCollisionObjectType(FFZSCollision::FacilityPlayerObjectChannel);
	FacilityBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	FacilityBody->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	FacilityBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FacilityBody->SetGenerateOverlapEvents(false);
	FacilityBody->SetCanEverAffectNavigation(false);
	FacilityBody->RegisterComponent();
	FacilityBody->SetWorldLocation(FVector(0.0, 0.0, 100.0));
	FacilityBody->SetWorldScale3D(FVector(1.0, 8.0, 4.0));

	UCapsuleComponent* PlayerCapsule = Player->GetCapsuleComponent();
	UCapsuleComponent* OtherCapsule = OtherCharacter->GetCapsuleComponent();
	TestEqual(TEXT("Player constructor opts into facility collision"),
		PlayerCapsule->GetCollisionResponseToChannel(FFZSCollision::FacilityPlayerObjectChannel), ECR_Block);
	TestEqual(TEXT("Ordinary characters retain the channel's default Ignore response"),
		OtherCapsule->GetCollisionResponseToChannel(FFZSCollision::FacilityPlayerObjectChannel), ECR_Ignore);
	TestEqual(TEXT("Player remains a Pawn for existing gameplay collision"),
		PlayerCapsule->GetCollisionObjectType(), ECC_Pawn);
	TestEqual(TEXT("Player still blocks static ground and walls"),
		PlayerCapsule->GetCollisionResponseToChannel(ECC_WorldStatic), ECR_Block);
	TestEqual(TEXT("Player still blocks existing dynamic obstacles"),
		PlayerCapsule->GetCollisionResponseToChannel(ECC_WorldDynamic), ECR_Block);

	// Emulate an existing Blueprint's serialized component response overriding the
	// new C++ default. Route the normal actor BeginPlay after component initialization.
	World->InitializeActorsForPlay(FURL());
	TestFalse(TEXT("Fixture leaves player BeginPlay pending"), Player->HasActorBegunPlay());
	PlayerCapsule->SetCollisionResponseToChannel(FFZSCollision::FacilityPlayerObjectChannel, ECR_Ignore);
	Player->DispatchBeginPlay();
	TestTrue(TEXT("Player BeginPlay ran"), Player->HasActorBegunPlay());
	TestEqual(TEXT("BeginPlay repairs the serialized Blueprint Ignore response"),
		PlayerCapsule->GetCollisionResponseToChannel(FFZSCollision::FacilityPlayerObjectChannel), ECR_Block);
	TestEqual(TEXT("BeginPlay preserves Pawn object type"), PlayerCapsule->GetCollisionObjectType(), ECC_Pawn);
	TestEqual(TEXT("BeginPlay preserves static world blocking"),
		PlayerCapsule->GetCollisionResponseToChannel(ECC_WorldStatic), ECR_Block);
	TestEqual(TEXT("BeginPlay preserves dynamic world blocking"),
		PlayerCapsule->GetCollisionResponseToChannel(ECC_WorldDynamic), ECR_Block);

	// A single long swept movement crosses the entire obstacle. Checking both the
	// hit component and final position catches configurations that only report a hit.
	FHitResult PlayerHit;
	PlayerCapsule->MoveComponent(FVector(500.0, 0.0, 0.0), PlayerCapsule->GetComponentQuat(), true, &PlayerHit);
	TestTrue(TEXT("Actual player capsule sweep is blocked"), PlayerHit.bBlockingHit);
	TestEqual(TEXT("The authored mesh collision produced the blocking hit"),
		PlayerHit.GetComponent(), static_cast<UPrimitiveComponent*>(FacilityBody));
	TestTrue(TEXT("Player stops outside the mesh instead of tunneling through it"),
		PlayerCapsule->GetComponentLocation().X <= -50.0 - PlayerCapsule->GetScaledCapsuleRadius() + 1.0);

	FHitResult OtherHit;
	OtherCapsule->MoveComponent(FVector(500.0, 0.0, 0.0), OtherCapsule->GetComponentQuat(), true, &OtherHit);
	TestFalse(TEXT("The same mesh does not block an ordinary character"), OtherHit.bBlockingHit);
	TestTrue(TEXT("Ordinary character completes the same swept distance"),
		FMath::IsNearlyEqual(OtherCapsule->GetComponentLocation().X, 250.0, 0.1));
	return true;
}

// 달리기 토글. 누를 때마다 켜고 끄며, 이동 속도가 상태를 따라간다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFF_FestivalCharacterSprintToggleTest,
	"FestaFiesta.Character.Sprint.ToggleFollowsState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFF_FestivalCharacterSprintToggleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game))
	{
		Fixture.ForwardErrorMessages(this);
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFF_FestivalCharacter* Player = Fixture.GetTestWorld()->SpawnActor<AFF_FestivalCharacter>(
		AFF_FestivalCharacter::StaticClass(), FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator, SpawnParameters);
	if (!TestNotNull(TEXT("캐릭터"), Player))
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
	TestTrue(TEXT("기본은 토글 방식"), Player->bToggleSprint);
	TestTrue(TEXT("달리기 속도가 걷기보다 빠르다"), Player->SprintSpeed > Player->WalkSpeed);
	TestFalse(TEXT("처음에는 걷는다"), Player->IsSprinting());

	// 두 번 뒤집으면 제자리로 돌아오고, 매번 속도가 상태를 따라간다.
	for (int32 Press = 1; Press <= 4; ++Press)
	{
		Player->ToggleSprint();
		const bool bExpectSprint = Press % 2 == 1;
		TestEqual(*FString::Printf(TEXT("%d번 누른 뒤 달리기 상태"), Press), Player->IsSprinting(), bExpectSprint);
		TestEqual(*FString::Printf(TEXT("%d번 누른 뒤 속도"), Press), Movement->MaxWalkSpeed,
			bExpectSprint ? Player->SprintSpeed : Player->WalkSpeed);
	}

	// 같은 값을 다시 걸어도 상태가 뒤집히지 않는다.
	Player->SetSprinting(true);
	Player->SetSprinting(true);
	TestTrue(TEXT("켠 채로 다시 켜도 달린다"), Player->IsSprinting());
	return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
