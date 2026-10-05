// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FF_FestivalCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UDamageType;
class UInputMappingContext;
class USpringArmComponent;
struct FInputActionValue;

// 플레이어가 직접 조종하는 축제 방문객.
//
// 관찰 카메라(AFF_OverviewCameraPawn)와 역할이 다르다. 저쪽은 축제 전체를 내려다보며
// 배치와 흐름을 보는 용도고, 이쪽은 방문객 눈높이에서 걸어다니는 체험용이다.
// 둘 사이 전환은 AFF_FestivalPlayerController 가 담당한다.
//
// 이 캐릭터는 Crowd 시뮬레이션 대상이 아니다. NPC 군중은 FUnitData 배열로 일괄 처리되지만
// 플레이어는 CharacterMovement 로 직접 움직인다. 회피 계산에 끼어들지 않는다는 뜻이라
// 군중을 밀고 지나갈 수 있는데, 그건 Crowd 쪽에서 플레이어를 장애물로 인식하게 만들어야
// 해결된다. 지금 범위 밖이다.
//
// 입력은 캐릭터 자신이 받는다. 컨트롤러가 아니라 여기에 두는 이유는, 빙의가 옮겨오면
// Enhanced Input 이 자동으로 이 폰의 바인딩을 살리고 나갈 때 죽이기 때문이다.
// 컨트롤러에 두면 관찰 카메라 조작과 같은 키를 두고 직접 교통정리를 해야 한다.
UCLASS(Blueprintable)
class FESTAFIESTA_API AFF_FestivalCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AFF_FestivalCharacter();

	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void UnPossessed() override;

	// 맵 밖으로 떨어졌을 때. 기본 동작은 액터를 파괴하므로, 돌아갈 땅이 있으면 그리로 보낸다.
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;

	/* 걸어온 길에서 안전한 땅으로 되돌아간다. 옮겼으면 true.

	   강, 구멍, 맵 가장자리 같은 위험 지형이 각자 위치를 기록하지 않도록 기록은 캐릭터가 갖고,
	   위험 지형은 감지만 해서 이 함수를 부른다.

	   가장 최근 기록은 낭떠러지 끝이라 다시 미끄러지기 쉽다.
	   그래서 걸어온 길을 RestoreBackoffCm 만큼 거슬러 올라간 지점으로 보낸다.
	*/
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|SafeLocation")
	bool RestoreToLastSafeLocation();

	// 되돌아갈 땅 기록이 있는지.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|SafeLocation")
	bool HasSafeLocation() const { return !SafeLocations.IsEmpty(); }

	// 안전 위치를 확인하는 주기. 조종 중이고 땅에 서 있을 때만 기록한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FestaFiesta|SafeLocation", meta = (ClampMin = "0.02"))
	float SafeSampleIntervalSeconds = 0.1f;

	// 이보다 가까운 위치는 새로 기록하지 않는다. 가만히 서 있어도 기록이 채워지는 것을 막는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FestaFiesta|SafeLocation", meta = (ClampMin = "10.0"))
	float SafeSampleSpacingCm = 50.f;

	// 기록 개수 상한. 간격과 곱한 길이만큼의 길을 기억한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FestaFiesta|SafeLocation", meta = (ClampMin = "2"))
	int32 MaxSafeSamples = 40;

	// 되돌아갈 때 걸어온 길을 이만큼 거슬러 올라간다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FestaFiesta|SafeLocation", meta = (ClampMin = "0.0"))
	float RestoreBackoffCm = 250.f;

	// 도착 지점에서 위로 띄우는 높이. 바닥에 파묻혀 끼는 것을 막는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FestaFiesta|SafeLocation", meta = (ClampMin = "0.0"))
	float RestoreLiftCm = 10.f;

	/* 시야 고정을 켜고 끈다. 로블록스의 시프트락과 같은 동작이다.

	   끈 상태는 카메라만 돌고 캐릭터는 걷는 방향을 본다. 주변을 둘러보기 좋다.
	   켠 상태는 캐릭터가 카메라를 따라 돈다. 조준하듯 움직일 때 편하다.
	*/
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Character")
	void SetShiftLock(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Character")
	bool IsShiftLockEnabled() const { return bShiftLock; }

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Character")
	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Character")
	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	// 지금 달리는 중인지. 애니메이션 블루프린트가 읽는다.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Character")
	bool IsSprinting() const { return bSprinting; }

	// 달리기를 켜고 끈다. 입력 말고 다른 곳(UI·연출)에서 걷기로 돌려놓을 때도 쓴다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Character")
	void SetSprinting(bool bEnabled);

	// 달리기를 뒤집는다. 토글 입력이 부른다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Character")
	void ToggleSprint() { SetSprinting(!bSprinting); }

	// ── 입력 자산 ────────────────────────────────────────────────────────────
	// 비어 있으면 그 동작만 조용히 빠진다. 하나 안 넣었다고 캐릭터가 죽으면 곤란하다.

	UPROPERTY(EditDefaultsOnly, Category = "FestaFiesta|Input")
	TObjectPtr<UInputMappingContext> CharacterMappingContext;

	// 관찰 카메라 컨텍스트보다 우선순위를 높게 둔다. 같은 키가 겹쳐도 이쪽이 이긴다.
	UPROPERTY(EditDefaultsOnly, Category = "FestaFiesta|Input")
	int32 CharacterMappingPriority = 10;

	// Axis2D. X 는 좌우, Y 는 전후.
	UPROPERTY(EditDefaultsOnly, Category = "FestaFiesta|Input")
	TObjectPtr<UInputAction> MoveAction;

	// Axis2D. 마우스 이동으로 시점 회전.
	UPROPERTY(EditDefaultsOnly, Category = "FestaFiesta|Input")
	TObjectPtr<UInputAction> LookAction;

	// Digital.
	UPROPERTY(EditDefaultsOnly, Category = "FestaFiesta|Input")
	TObjectPtr<UInputAction> JumpAction;

	/* Digital. 누를 때마다 달리기를 켜고 끈다(bToggleSprint).

	   키는 코드에 두지 않는다. IMC_Character 에 이 액션과 키를 넣으면 된다.
	   LeftShift 는 시야 고정(ShiftLockAction)이 쓰고 있어 겹치지 않게 고른다.
	*/
	UPROPERTY(EditDefaultsOnly, Category = "FestaFiesta|Input")
	TObjectPtr<UInputAction> SprintAction;

	/* 켜면 누를 때마다 달리기를 켜고 끈다. 끄면 누르는 동안만 달린다(예전 동작).

	   축제장은 넓어서 오래 달리는 일이 많다. 누르고 있게 하면 이동 키와 함께 손가락이 묶인다.
	*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FestaFiesta|Input")
	bool bToggleSprint = true;

	/* 시야 고정 전환. 로블록스의 시프트락과 같은 동작이다.

	   끈 상태는 카메라만 돌고 캐릭터는 걷는 방향을 본다. 주변을 둘러보기 좋다.
	   켠 상태는 캐릭터가 카메라를 따라 돈다. 조준하듯 움직일 때 편하다.
	*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FestaFiesta|Character|Input")
	TObjectPtr<UInputAction> ShiftLockAction;

	// ── 이동 ─────────────────────────────────────────────────────────────────

	// 걷기 속도. 축제 방문객은 관람하며 다니므로 표준 600 보다 느리게 잡는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FestaFiesta|Movement", meta = (ClampMin = "50.0"))
	float WalkSpeed = 380.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FestaFiesta|Movement", meta = (ClampMin = "50.0"))
	float SprintSpeed = 700.f;

	// 마우스 감도 배수.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FestaFiesta|Input", meta = (ClampMin = "0.05"))
	float LookSensitivity = 1.f;

protected:
	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);

	void HandleShiftLockToggled(const FInputActionValue& Value);

	/* 시야 고정 상태.

	   켜면 캐릭터가 컨트롤러 회전을 따라가고 이동 방향 정렬을 끈다.
	   두 가지를 동시에 바꿔야 한다. 하나만 바꾸면 한쪽은 카메라를 따라 돌리려 하고
	   다른 쪽은 걷는 방향으로 돌리려 해서 캐릭터가 떨린다.
	*/
	bool bShiftLock = false;

	void HandleJumpStarted(const FInputActionValue& Value);
	void HandleJumpCompleted(const FInputActionValue& Value);
	void HandleSprintStarted(const FInputActionValue& Value);
	void HandleSprintCompleted(const FInputActionValue& Value);

	void AddMappingContext();
	void RemoveMappingContext();
	void ApplyMovementSpeed();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FestaFiesta|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FestaFiesta|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

private:
	void SampleSafeLocation();

	bool bSprinting = false;

	// 땅 위에서 지나온 위치. 오래된 것이 앞이다.
	TArray<FVector> SafeLocations;

	FTimerHandle SafeSampleTimer;
	bool bWarnedNoSafeLocation = false;
};
