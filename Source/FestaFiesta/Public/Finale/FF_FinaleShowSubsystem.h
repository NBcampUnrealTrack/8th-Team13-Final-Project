// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/FF_DebugConsole.h"
#include "Economy/FF_FestivalEconomyTypes.h"
#include "Time/FF_FestivalStage.h"
#include "Finale/FF_FinaleShowTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Subsystems/WorldSubsystem.h"
#include "FF_FinaleShowSubsystem.generated.h"

class AFF_FinaleViewpoint;
class AFF_FinaleLaunchSite;
class AFF_FinaleShellTracer;
class AFF_FestivalPlayerController;

// 연출 진행 상태가 바뀔 때. UI 가 표시를 바꾸거나 다른 시스템이 멈출 때 쓴다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFF_OnFinaleShowStateChanged, bool, bInProgress);

// 한 발 터질 때마다. 사운드나 화면 흔들림을 붙이고 싶으면 여기에 건다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFF_OnFinaleBurst, FVector, WorldLocation, FLinearColor, Color);

// 3막 연출에서 막이 바뀔 때. 엔딩곡처럼 막에 맞춰 바뀌어야 하는 것을 여기에 건다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFF_OnFinaleTourActChanged, EFF_FinaleTourAct, NewAct);

// 바지선에서 한 발 쏘아 올릴 때. 발사음(쿵)은 여기에 건다. RiseSeconds 뒤에 BurstLocation 에서 OnBurst 가 온다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FFF_OnFinaleLaunch, FVector, LaunchLocation, FVector, BurstLocation, float, RiseSeconds);

// 피날레 불꽃 연출을 실행한다.
//
// ─── 경계 ───────────────────────────────────────────────────────────────────
// 등급은 경제·축제 쪽이 소유한다. 여기서는 읽기만 하고 구매나 판정에 관여하지 않는다.
// 카메라 전환은 컨트롤러에 요청만 하고 직접 시점을 만들지 않는다.
//
// ─── 아무것도 없어도 돌아야 한다 ────────────────────────────────────────────
// 레벨도 이펙트 에셋도 아직 없는 단계다. 발사 지점이 없으면 원점 위에서 터뜨리고,
// 이펙트가 없으면 로그만 남긴다. 어느 경우에도 크래시나 정지가 없어야
// 다른 파트 작업이 이것 때문에 막히지 않는다.
UCLASS()
class FESTAFIESTA_API UFF_FinaleShowSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// 지금 구매된 등급으로 연출을 시작한다.
	// 이미 진행 중이면 아무것도 하지 않는다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Finale")
	bool StartShow();

	// 등급을 직접 지정해서 시작한다. 연출 확인용이다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Finale")
	bool StartShowWithGrade(EFF_FinaleGrade Grade);

	// 진행 중인 연출을 중단하고 시점을 되돌린다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Finale")
	void StopShow();

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Finale")
	bool IsShowInProgress() const { return bShowInProgress; }

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Finale")
	EFF_FinaleGrade GetActiveGrade() const { return ActiveGrade; }

	// 지금까지 몇 발 터졌는지. 0 에서 안 올라가면 발사가 안 되고 있는 것이다.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Finale")
	int32 GetFiredBurstCount() const { return FiredBursts; }

	// 지금까지 바지선에서 쏘아 올린 알 수. 오르는 중인 알도 센다. 모양 알은 여러 발을 품어도 하나다.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Finale")
	int32 GetLaunchedShellCount() const { return LaunchedShells; }

	// 지금 오르는 중인 발수. 모양 알 하나는 품은 발을 전부 센다(터지면 그만큼 피어난다).
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Finale")
	int32 GetShellsInFlight() const { return InFlightBurstCount; }

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Finale")
	float GetElapsedSeconds() const { return ElapsedSeconds; }

	/* 카메라 순회를 끊고 원래 시점으로 돌아간다. 불꽃은 계속 터진다.

	   두 번째 플레이부터는 영상을 다시 보고 싶지 않다. 그렇다고 연출을 통째로
	   끝내면 마지막을 못 보므로, 카메라만 놓아준다.
	*/
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Finale")
	void SkipCinematicCamera();

	// 지금 카메라 순회 중인지. 스킵 안내를 띄울지 정하는 데 쓴다.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Finale")
	bool IsCinematicTourActive() const { return TourViewpoints.Num() > 1 && !bTourSkipped; }

	// 이번 연출이 3막(불꽃 관람 → 축제장 순회 → 마무리)으로 도는지.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Finale")
	bool IsActTourActive() const { return bActMode; }

	// 지금 막. 3막이 아니면 늘 불꽃 관람이다.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Finale")
	EFF_FinaleTourAct GetCurrentAct() const { return CurrentAct; }

	// 축제가 피날레 단계에 들어가면 자동으로 시작할지.
	//
	// 끄면 StartShow 를 직접 불러야 한다. 연출을 다듬는 동안에는 꺼두고 콘솔로
	// 재생하는 편이 편하다. 단계를 넘기려면 축제를 끝까지 진행해야 하기 때문이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FestaFiesta|Finale")
	bool bAutoStartOnFinaleStage = true;

	// 연출 중 카메라를 연출 시점으로 옮길지. 끄면 현재 시점에서 그대로 본다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Finale")
	void SetCameraTakeoverEnabled(bool bEnabled) { bTakeOverCamera = bEnabled; }

	UPROPERTY(BlueprintAssignable, Category = "FestaFiesta|Finale")
	FFF_OnFinaleShowStateChanged OnShowStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "FestaFiesta|Finale")
	FFF_OnFinaleBurst OnBurst;

	UPROPERTY(BlueprintAssignable, Category = "FestaFiesta|Finale")
	FFF_OnFinaleTourActChanged OnTourActChanged;

	UPROPERTY(BlueprintAssignable, Category = "FestaFiesta|Finale")
	FFF_OnFinaleLaunch OnLaunch;

	// 등급별 수치를 직접 지정한다. 보통은 비워두고 GameMode 나 Project Settings 를 쓴다.
	//
	// 월드 서브시스템은 에디터에 노출되지 않으므로 이 값은 코드에서만 채울 수 있다.
	// 연출을 임시로 바꿔보고 싶을 때 쓰는 자리다.
	UPROPERTY(BlueprintReadWrite, Category = "FestaFiesta|Finale")
	TSoftObjectPtr<UFF_FinaleShowData> ShowData;

	// 연출 시작·종료 시 카메라 블렌드 시간.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FestaFiesta|Finale", meta = (ClampMin = "0.0"))
	float CameraBlendSeconds = 1.5f;

	// 마지막 발이 터진 뒤 시점을 되돌리기까지 기다리는 시간.
	// 바로 돌아가면 마지막 불꽃이 화면에서 잘린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FestaFiesta|Finale", meta = (ClampMin = "0.0"))
	float TailSeconds = 3.f;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UFF_FinaleShowSubsystem, STATGROUP_Tickables);
	}

private:
	// 축제 단계 신호를 구독한다. 단계는 축제 진행 쪽이 소유하고 여기서는 읽기만 한다.
	void BindFestivalStage();
	void UnbindFestivalStage();

	UFUNCTION()
	void HandleFestivalStageChanged(EFF_FestivalStage PreviousStage, EFF_FestivalStage NewStage);

#if !UE_BUILD_SHIPPING
	// ── 디버그 콘솔 ──────────────────────────────────────────────────────────
	//
	// 이펙트를 만들려면 계속 터뜨려 봐야 하는데, 그때마다 축제를 끝까지 진행할 수는 없다.
	//
	// UFUNCTION(Exec) 을 쓰지 않는 이유는 UGameInstance 에 ProcessConsoleExec 이 없어
	// 콘솔이 서브시스템의 Exec 함수를 찾아가지 않기 때문이다. IConsoleManager 에 직접 등록한다.
	// 해제를 반드시 하는 이유는 PIE 가 한 에디터 세션에서 여러 번 돌기 때문이다.
	// 남겨두면 두 번째 PIE 에서 같은 이름 등록이 실패하고, 파괴된 this 를 가리키는
	// 델리게이트가 콘솔에 남는다.
	void RegisterDebugConsoleCommands();
	void UnregisterDebugConsoleCommands();

	// 문자열을 등급으로 바꾼다. "S" "a" "none" 전부 받는다.
	static bool TryParseGrade(const FString& Token, EFF_FinaleGrade& OutGrade);

	// 개발용 콘솔 명령. 이름 충돌·해제 규칙은 FFF_DebugConsoleCommands 가 맡는다.
	FFF_DebugConsoleCommands DebugCommands;
#endif

	// 이펙트에 우리가 넣으려는 User 파라미터가 실제로 있는지 확인하고, 없으면 알린다.
	//
	// 이름이 틀리면 Niagara 는 조용히 무시한다. 크래시도 경고도 없어서
	// "왜 색이 안 바뀌지" 로만 나타나고, 이펙트 쪽을 한참 뒤진 뒤에야 이름 문제인 걸 안다.
	// 연출당 한 번만 검사한다. 매 발 찍으면 로그가 넘친다.
	void VerifyEffectParameters(const UNiagaraSystem& Effect) const;

	// 이번 연출에서 이미 검사한 이펙트. 같은 것을 반복해서 검사하지 않는다.
	mutable TWeakObjectPtr<const UNiagaraSystem> VerifiedEffect;

	// 이번 등급의 수치를 결정한다. 데이터 애셋이 있으면 그것을, 없으면 기본값을 쓴다.
	FFF_FinaleGradeSpec ResolveSpec(EFF_FinaleGrade Grade) const;

	// 쓸 데이터 애셋을 고른다. 직접 지정 → GameMode → Project Settings 순으로 본다.
	//
	// 레벨마다 연출이 다를 수 있어서 GameMode 가 전역 설정보다 우선한다.
	// 전역 기본값을 둔 이유는 새 레벨을 만들 때마다 매번 지정하지 않아도 되게 하려는 것이다.
	const UFF_FinaleShowData* ResolveShowData() const;

	// 한 차례 발사. SimultaneousShells 만큼 동시에 터뜨린다.
	void FireVolley();

	/* 이번 등급에 쓸 시점을 모은다. 개수 제한을 적용한다.

	   시점이 하나뿐이면 순회할 것이 없으므로 고정 연출과 같아진다.
	   그래서 순회 여부를 따로 두지 않고 개수만으로 표현한다.
	*/
	void BuildTour();

	// 다음 시점으로 옮긴다. 마지막이면 첫 자리로 돌아간다.
	// 3막이면 돌아가지 않고 마지막(마무리) 자리에 머문다.
	void AdvanceTour();

	// 막을 바꾸고 알린다. 같은 막이면 아무것도 하지 않는다.
	void SetCurrentAct(EFF_FinaleTourAct NewAct);

	/* 지금 발사해도 되는지.

	   3막이 아니면 남은 발이 있는 한 열려 있다. 3막이면 축제장 순회 동안 닫히고,
	   불꽃 관람 막에서는 마무리 몫을 남기고 닫힌다.
	*/
	bool IsFiringGateOpen() const;

	// 마무리 막이면 간격을 줄인다. 그 밖에는 1.
	float GetActIntervalScale() const;

	// 남은 발을 모든 발사대에서 한꺼번에 올린다. 3막 마무리의 마지막 한 방이다.
	void FireFinalSalvo();

	// 지금 시점으로 카메라를 옮긴다.
	void ApplyTourViewpoint(float BlendSeconds);

	/* 이번 차례에 쓸 패턴을 고른다. 없으면 단발.

	   남은 발수를 받는 이유는, 모양이 있는 패턴은 발수가 곧 모양이기 때문이다.
	   12발짜리 원을 3발로 깎으면 원이 아니라 점 세 개가 된다. 그렇게 끝나느니
	   그 차례는 단발을 쏜다.
	*/
	const FFF_FinaleVolley& PickVolley(int32 RemainingShells) const;

	// 한 발을 실제로 터뜨린다. 패턴이 정한 자리든 흩뿌린 자리든 여기로 모인다.
	// SizeScale 은 등급 크기에 곱한다. 높이에 따른 구경 차이를 여기로 넘긴다.
	void SpawnShell(const FVector& Location, const FLinearColor& Color, class UNiagaraSystem* Effect,
		float SizeScale = 1.f);

	/* 한 발을 Origin(바지선)에서 쏘아 올린다. RiseSeconds 뒤에 Location 에서 터진다(SpawnShell).

	   RiseSeconds 가 0 이하면 터질 높이로 상승 시간을 낸다. 모양 패턴은 차례 전체에 같은 시간을
	   넘긴다 — 발마다 높이가 달라 시간이 갈리면 하트가 한꺼번에 피지 않는다.
	   발사가 꺼져 있으면(FFF_FinaleLaunchSpec::bLaunchFromSite) 바로 터뜨린다.
	*/
	void LaunchShell(const FVector& Origin, const FVector& Location, const FLinearColor& Color,
		class UNiagaraSystem* Effect, float SizeScale, float RiseSeconds = 0.f);

	// 오르는 알이 터질 때 피는 불꽃 하나.
	struct FShellBurst
	{
		FVector Location = FVector::ZeroVector;
		FLinearColor Color = FLinearColor::White;
		TWeakObjectPtr<class UNiagaraSystem> Effect;
		float SizeScale = 1.f;
	};

	/* 알 하나를 Target 까지 쏘아 올리고, 다 오르면 Bursts 를 한꺼번에 터뜨린다.

	   하트·원처럼 한 순간에 피는 모양은 실제로 알 하나가 터지며 그린다(모양 불꽃). 예전처럼
	   발마다 따로 쏘아 올리면 알 열몇 개가 다발로 올라가 해파리처럼 보였다(2026-10-05 화면 확인).
	   한 발짜리는 Bursts 가 하나다.
	*/
	void LaunchCarrier(const FVector& Origin, const FVector& Target, TArray<FShellBurst>&& Bursts,
		const FLinearColor& Color, float RiseSeconds);

	// 오르는 발을 옮기고, 다 오른 발을 터뜨린다. 터뜨리다 연출이 멈추면 false.
	bool TickInFlightShells(float DeltaTime);

	// 지금 켜진 번쩍임 빛의 수. 동시 조명 상한을 지킨다.
	int32 CountActiveFlashLights() const;

	// 이번 연출의 발사 방식. 데이터 애셋이 없으면 구조체 기본값(시간·궤적만, 모습 없음)이다.
	FFF_FinaleLaunchSpec ResolveLaunchSpec() const;

	/* 레벨에 발사 지점·연출 시점이 없으면 데이터의 기본 배치로 세운다(FFF_FinaleLayoutLaunchSite 주석).

	   레벨에 놓은 액터가 있으면 그 종류는 손대지 않는다. 한 월드에서 한 번만 세운다.
	*/
	void EnsureFallbackLayout();

	// 아직 쏘지도 예약하지도 않은 발수. 연출의 끝을 이것으로 정한다.
	int32 GetRemainingShells() const;

	// 이펙트 칸 상태를 한 줄로. 콘솔과 경고 로그가 같은 표기를 쓰게 한다.
	static FString DescribeEffects(const FFF_FinaleGradeSpec& Spec);

	// 지연 발사 대기열. 일렬이나 부챗살은 한 프레임에 다 쏘지 않는다.
	struct FPendingShell
	{
		// 터질 자리.
		FVector Location = FVector::ZeroVector;

		// 쏘아 올릴 바지선 자리.
		FVector Origin = FVector::ZeroVector;

		// 상승 시간. 0 이하면 쏠 때 높이로 낸다(LaunchShell).
		float RiseSeconds = 0.f;

		FLinearColor Color = FLinearColor::White;
		float DelayRemaining = 0.f;

		// 예약할 때 고른 이펙트를 들고 있는다.
		//
		// 터질 때 다시 고르면 한 패턴 안에서 종류가 섞인다. 일렬로 날아가는데
		// 모양이 제각각이면 줄로 안 보인다.
		TWeakObjectPtr<class UNiagaraSystem> Effect;

		float SizeScale = 1.f;
	};

	TArray<FPendingShell> PendingShells;

	// 쏘아 올려 오르는 중인 알. 다 오르면 품은 불꽃을 터뜨린다.
	struct FInFlightShell
	{
		FVector Origin = FVector::ZeroVector;
		FVector Target = FVector::ZeroVector;
		TArray<FShellBurst> Bursts;
		float RiseSeconds = 1.f;
		float Elapsed = 0.f;
		float WobblePhase = 0.f;
		TWeakObjectPtr<AFF_FinaleShellTracer> Tracer;
	};

	TArray<FInFlightShell> InFlightShells;

	// 오르는 알들이 품은 발수의 합. 남은 발수·막의 몫을 셀 때 이것을 뺀다.
	int32 InFlightBurstCount = 0;

	// 이번 연출의 발사 방식. 시작할 때 한 번 정한다.
	FFF_FinaleLaunchSpec ActiveLaunch;

	// 이 월드에서 기본 배치를 이미 세웠는지.
	bool bFallbackLayoutApplied = false;

	/* 축제장 순회 경로를 만든다. 입구(손님이 들어오는 게이트)와 설치된 시설을 읽는다.

	   만들었으면 순회 카메라를 경로 시작에 놓고 true. 입구도 시설도 없으면 false 이고,
	   그때는 레벨·데이터의 고정 순회 자리를 쓴다(FFF_FinaleSiteTourSpec 주석).
	*/
	bool PrepareSiteTour();

	// 순회 카메라를 경로 위 HeldSeconds 지점으로 옮긴다.
	void UpdateSiteTourCamera(float HeldSeconds, bool bSnap);

	// 이번 연출의 순회 설정과 경로.
	FFF_FinaleSiteTourSpec ActiveSiteTour;
	FFF_FinaleSiteTourRoute SiteTourRoute;
	float SiteTourSeconds = 0.f;

	// 경로를 따라 움직이는 순회 카메라. 한 번 만들어 연출마다 다시 쓴다.
	UPROPERTY(Transient)
	TObjectPtr<AFF_FinaleViewpoint> SiteTourCamera;

	// 이번 연출에서 돌 시점들. 하나뿐이면 고정 연출이다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<AFF_FinaleViewpoint>> TourViewpoints;

	int32 TourIndex = 0;

	// 지금 자리에 머문 시간. 체류 시간을 채우면 다음으로 넘어간다.
	float TourHeldSeconds = 0.f;

	// 이번 연출에서 카메라 순회를 끊었는지. 끊은 뒤에는 다시 잡지 않는다.
	bool bTourSkipped = false;

	// TourViewpoints 와 같은 순서로 각 자리의 막을 든다.
	// 마무리 시점이 없으면 첫 불꽃 관람 자리를 마무리로 한 번 더 쓰므로, 자리의 설정값과 다를 수 있다.
	TArray<EFF_FinaleTourAct> TourActs;

	// 이번 연출이 3막인지. BuildTour 에서 정한다.
	bool bActMode = false;

	EFF_FinaleTourAct CurrentAct = EFF_FinaleTourAct::Fireworks;

	// 불꽃 관람 막에서 쏠 수 있는 발수. 나머지는 마무리 막 몫이다.
	int32 ActOneQuota = 0;

	/* 발사용 시계. 발사가 열려 있거나 예약된 발이 남아 있을 때만 흐른다.

	   축제장 순회 동안 발사를 멈추려면 발사 간격 계산도 같이 멈춰야 한다.
	   전체 경과 시간으로 재면 순회를 마치고 돌아온 순간 밀린 간격을 한꺼번에 따라잡으려 든다.
	   3막이 아니면 발사가 끝날 때까지 늘 열려 있어 전체 경과 시간과 같게 흐른다.
	*/
	float FiringClock = 0.f;

	// 동시 발수가 지나치게 많을 때 한 번만 알린다.
	bool bWarnedHighShellCount = false;

	// 이펙트 없이 쏘고 있다는 것을 한 번만 알린다.
	bool bWarnedMissingEffect = false;

	// 발사 시간 상한을 넘겼다는 것을 한 번만 알린다.
	bool bWarnedDeadline = false;

	// 발사 지점을 구한다. 레벨에 발사대가 없으면 원점 위를 쓴다.
	FVector ResolveLaunchOrigin() const;

	// 바지선까지 펼친 발사 위치 목록. 없으면 원점 하나를 담는다.
	void CollectLaunchPoints(TArray<FVector>& OutPoints) const;

	// 터질 지점 하나를 뽑는다. 반경 안에 흩고 높이도 범위 안에서 고른다.
	FVector PickBurstLocation(const FVector& Origin) const;

	/* 모양이 마주봐야 할 방향. 중심에서 관객 쪽으로 향하는 수평 벡터다.

	   수직면에 그리는 모양은 방향이 틀리면 선으로 보인다. 그 방향을 아는 것은
	   지금 어느 시점을 쓰는지 아는 여기뿐이라, 패턴에 값으로 넘겨준다.
	*/
	FVector ResolveAudienceDirection(const FVector& Center) const;

	FLinearColor PickColor(int32 Index) const;

	AFF_FestivalPlayerController* GetFestivalController() const;

	// 게임 인스턴스 서브시스템을 꺼낸다. 월드와 인스턴스 널체크가 매번 반복돼서 묶었다.
	template <typename SubsystemType>
	SubsystemType* GetGameInstanceSubsystem() const
	{
		const UWorld* World = GetWorld();
		return UGameInstance::GetSubsystem<SubsystemType>(World ? World->GetGameInstance() : nullptr);
	}

	bool bShowInProgress = false;
	bool bTakeOverCamera = true;

	EFF_FinaleGrade ActiveGrade = EFF_FinaleGrade::None;
	FFF_FinaleGradeSpec ActiveSpec;

	float ElapsedSeconds = 0.f;
	float NextVolleyAt = 0.f;
	int32 FiredBursts = 0;
	int32 FiredVolleys = 0;
	int32 LaunchedShells = 0;

	// 마지막 발 이후 여운 시간. 음수면 아직 발사가 안 끝난 것이다.
	float TailStartedAt = -1.f;

	// 색을 순서대로 돌린다. 같은 색이 연달아 나오면 등급이 낮아 보인다.
	int32 ColorCursor = 0;

	FRandomStream ShowRandom;

	// 단계 신호에 물려 있는지. 해제할 때 중복 호출을 막는다.
	bool bStageBound = false;
};
