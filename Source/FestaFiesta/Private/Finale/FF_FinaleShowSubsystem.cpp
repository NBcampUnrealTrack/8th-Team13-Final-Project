// Copyright Epic Games, Inc. All Rights Reserved.

#include "Finale/FF_FinaleShowSubsystem.h"

#include "Camera/CameraComponent.h"
#include "CrowdSystem/Systems/FF_CrowdSimulationSubsystem.h"
#include "CrowdSystem/Types/FF_FestivalCrowdTypes.h"
#include "FF_FFZSTypes.h"
#include "FF_FFZSWorldSubsystem.h"
#include "Camera/FF_FestivalPlayerController.h"
#include "Camera/FF_FinaleViewpoint.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Festival/FF_FestivalProgressionSubsystem.h"
#include "Time/FF_FestivalStageSubsystem.h"
#include "Camera/FF_FestivalGameMode.h"
#include "Finale/FF_FinaleLaunchSite.h"
#include "Finale/FF_FinaleSettings.h"
#include "Finale/FF_FinaleShellTracer.h"
#include "GameFramework/GameModeBase.h"
// GetPlayerViewPoint 를 직접 부른다. 컨트롤러 헤더가 끌고 오지만 명시해 둔다.
#include "GameFramework/PlayerController.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"

// 콘솔 명령의 이름 충돌·해제 규칙은 FFF_DebugConsoleCommands 가 맡는다(Debug/FF_DebugConsole.h).
// 예전에는 이 파일에만 그 규칙이 있었고, 다른 모듈은 절반만 지켰다.

bool UFF_FinaleShowSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFF_FinaleShowSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 시드를 고정하지 않는다. 불꽃은 매번 달라 보이는 편이 낫다.
	// 재현이 필요해지면 여기에 시드를 주입하는 경로를 열면 된다.
	ShowRandom.GenerateNewSeed();

#if !UE_BUILD_SHIPPING
	RegisterDebugConsoleCommands();
#endif
	BindFestivalStage();
}

void UFF_FinaleShowSubsystem::BindFestivalStage()
{
	if (bStageBound)
	{
		return;
	}

	UFF_FestivalStageSubsystem* Stage = GetGameInstanceSubsystem<UFF_FestivalStageSubsystem>();
	if (!Stage)
	{
		// 단계 시스템이 없는 테스트 맵이면 자동 시작만 못 할 뿐 콘솔 재생은 된다.
		UE_LOG(LogFestaFiestaFinale, Log,
			TEXT("[피날레] 축제 단계 시스템이 없어 자동 시작을 연결하지 않는다."));
		return;
	}

	Stage->OnStageChanged.AddDynamic(this, &UFF_FinaleShowSubsystem::HandleFestivalStageChanged);
	bStageBound = true;

	// 이미 피날레 단계에서 시작한 경우. 신호는 지나갔으므로 현재 값을 직접 본다.
	if (bAutoStartOnFinaleStage && Stage->CurrentStage == EFF_FestivalStage::Finale)
	{
		StartShow();
	}
}

void UFF_FinaleShowSubsystem::UnbindFestivalStage()
{
	if (!bStageBound)
	{
		return;
	}

	if (UFF_FestivalStageSubsystem* Stage = GetGameInstanceSubsystem<UFF_FestivalStageSubsystem>())
	{
		Stage->OnStageChanged.RemoveDynamic(this, &UFF_FinaleShowSubsystem::HandleFestivalStageChanged);
	}
	bStageBound = false;
}

void UFF_FinaleShowSubsystem::HandleFestivalStageChanged(
	const EFF_FestivalStage PreviousStage,
	const EFF_FestivalStage NewStage)
{
	(void)PreviousStage;

	// 피날레를 벗어나면 진행 중이던 연출을 접는다. 단계가 넘어갔는데 불꽃이
	// 계속 터지면 다음 화면 위에 얹힌다.
	if (NewStage != EFF_FestivalStage::Finale)
	{
		if (bShowInProgress)
		{
			StopShow();
		}
		return;
	}

	if (!bAutoStartOnFinaleStage)
	{
		return;
	}
	StartShow();
}

void UFF_FinaleShowSubsystem::Deinitialize()
{
	UnbindFestivalStage();
#if !UE_BUILD_SHIPPING
	UnregisterDebugConsoleCommands();
#endif

	if (bShowInProgress)
	{
		StopShow();
	}
	Super::Deinitialize();
}

#if !UE_BUILD_SHIPPING

bool UFF_FinaleShowSubsystem::TryParseGrade(const FString& Token, EFF_FinaleGrade& OutGrade)
{
	const FString Upper = Token.TrimStartAndEnd().ToUpper();

	if (Upper == TEXT("S")) { OutGrade = EFF_FinaleGrade::S; return true; }
	if (Upper == TEXT("A")) { OutGrade = EFF_FinaleGrade::A; return true; }
	if (Upper == TEXT("B")) { OutGrade = EFF_FinaleGrade::B; return true; }
	if (Upper == TEXT("C")) { OutGrade = EFF_FinaleGrade::C; return true; }
	if (Upper == TEXT("NONE") || Upper == TEXT("N")) { OutGrade = EFF_FinaleGrade::None; return true; }

	return false;
}

void UFF_FinaleShowSubsystem::RegisterDebugConsoleCommands()
{
	// 중복 등록 방어. 어떤 이유로 해제가 안 됐어도 두 번째 PIE 가 조용히 망가지지 않게 한다.
	UnregisterDebugConsoleCommands();

	DebugCommands.Add(
		TEXT("FF.Finale.Play"),
		TEXT("피날레 연출을 즉시 재생한다. 사용법: FF.Finale.Play [None|C|B|A|S]  등급 생략 시 구매한 등급"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			// 이미 돌고 있으면 갈아엎는다. 등급을 바꿔가며 비교할 때 매번 멈추기 번거롭다.
			if (bShowInProgress)
			{
				StopShow();
			}

			if (Args.Num() == 0)
			{
				StartShow();
				return;
			}

			EFF_FinaleGrade Grade = EFF_FinaleGrade::None;
			if (!TryParseGrade(Args[0], Grade))
			{
				UE_LOG(LogFestaFiestaFinale, Warning,
					TEXT("[피날레] 알 수 없는 등급입니다: %s (None, C, B, A, S 중 하나)"), *Args[0]);
				return;
			}
			StartShowWithGrade(Grade);
		}));

	DebugCommands.Add(
		TEXT("FF.Finale.Stop"),
		TEXT("진행 중인 피날레 연출을 중단하고 시점을 되돌린다."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			StopShow();
		}));

	DebugCommands.Add(
		TEXT("FF.Finale.SkipCamera"),
		TEXT("카메라 순회만 끊는다. 불꽃은 계속 터진다."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			SkipCinematicCamera();
		}));

	/* 발사 위치를 월드에 그린다.

	   바지선은 액터 하나에서 계산으로 펼쳐져 에디터 뷰포트에 따로 보이지 않는다.
	   바다 위에 제대로 늘어섰는지, 해변과 평행한지 PIE 에서 바로 확인하려고 둔다.
	*/
	DebugCommands.Add(
		TEXT("FF.Finale.ShowLaunchPoints"),
		TEXT("피날레 발사 위치(바지선 포함)를 10초 동안 월드에 표시한다."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			TArray<FVector> Points;
			CollectLaunchPoints(Points);
			const UWorld* World = GetWorld();
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				DrawDebugSphere(World, Points[Index], 800.f, 12, FColor::Orange, false, 10.f, 0, 40.f);
				DrawDebugLine(World, Points[Index], Points[Index] + FVector(0.f, 0.f, ActiveSpec.BurstHeightMax > 0.f
					? ActiveSpec.BurstHeightMax : 25000.f), FColor::Yellow, false, 10.f, 0, 20.f);
				DrawDebugString(World, Points[Index] + FVector(0.f, 0.f, 1500.f),
					FString::Printf(TEXT("발사 %d"), Index + 1), nullptr, FColor::White, 10.f);
			}
			UE_LOG(LogFestaFiestaFinale, Display, TEXT("[피날레] 발사 위치 %d곳을 표시했다."), Points.Num());
		}));

	DebugCommands.Add(
		TEXT("FF.Finale.Stat"),
		TEXT("현재 연출 상태와 등급별 수치를 출력한다."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogFestaFiestaFinale, Display,
				TEXT("[피날레] 진행=%s 등급=%d 발사=%d/%d 경과=%.1fs"),
				bShowInProgress ? TEXT("예") : TEXT("아니오"),
				static_cast<int32>(ActiveGrade), FiredBursts, ActiveSpec.BurstCount, ElapsedSeconds);

			// 쏘아 올리기 상태. 오르는 중인 발이 쌓이기만 하면 터뜨리는 쪽이 막힌 것이다.
			const FFF_FinaleLaunchSpec Launch = bShowInProgress ? ActiveLaunch : ResolveLaunchSpec();
			UE_LOG(LogFestaFiestaFinale, Display,
				TEXT("  쏘아 올리기=%s 올린 발=%d 오르는 중=%d 상승 %.1f~%.1f초(기준 %.0fm %.1f초) 꼬리=%s 빛=%s"),
				Launch.IsEnabled() ? TEXT("켬") : TEXT("끔"), LaunchedShells, InFlightBurstCount,
				Launch.ResolveRiseSeconds(ActiveSpec.BurstHeightMin), Launch.ResolveRiseSeconds(ActiveSpec.BurstHeightMax),
				Launch.ReferenceHeightCm / 100.f, Launch.ReferenceRiseSeconds,
				(Launch.TrailMesh.IsNull() || Launch.TrailMaterial.IsNull()) ? TEXT("없음") : TEXT("있음"),
				Launch.GlowMaterial.IsNull() ? TEXT("없음") : TEXT("있음"));

			// 등급별 수치를 같이 찍는다. 데이터 애셋을 고치고 나서 반영됐는지 바로 확인할 수 있다.
			for (const EFF_FinaleGrade Grade : FFF_FinaleGrades::GetAscendingOrder())
			{
				const FFF_FinaleGradeSpec Spec = ResolveSpec(Grade);
				UE_LOG(LogFestaFiestaFinale, Display,
					TEXT("  등급 %d: %d발 / %.0f초 / 동시 %d / %d차례 / 간격 %.2fs / 색 %d개 / 이펙트 %s"),
					static_cast<int32>(Grade), Spec.BurstCount, Spec.DurationSeconds,
					Spec.SimultaneousShells, Spec.GetVolleyCount(), Spec.GetIntervalSeconds(),
					Spec.Colors.Num(),
					*DescribeEffects(Spec));
			}

			// 어느 애셋이 먹었는지 같이 찍는다. 세 군데서 올 수 있어서
			// "값을 고쳤는데 왜 안 바뀌지" 가 나오기 쉽다.
			const UFF_FinaleShowData* Data = ResolveShowData();
			UE_LOG(LogFestaFiestaFinale, Display,
				TEXT("  사용 중인 데이터: %s"),
				Data ? *Data->GetName() : TEXT("없음 (코드 기본값)"));

			TArray<AFF_FinaleLaunchSite*> Sites;
			AFF_FinaleLaunchSite::CollectAll(GetWorld(), Sites);
			TArray<FVector> LaunchPoints;
			AFF_FinaleLaunchSite::CollectLaunchPoints(GetWorld(), LaunchPoints);
			UE_LOG(LogFestaFiestaFinale, Display,
				TEXT("  발사 지점 %d개 (바지선 포함 발사 위치 %d곳) / 연출 시점 %s"),
				Sites.Num(), LaunchPoints.Num(),
				AFF_FinaleViewpoint::FindBest(GetWorld()) ? TEXT("있음") : TEXT("없음"));

			TArray<AFF_FinaleViewpoint*> TourCandidates;
			AFF_FinaleViewpoint::CollectTour(GetWorld(), TourCandidates);
			UE_LOG(LogFestaFiestaFinale, Display,
				TEXT("  순회 가능 시점 %d개 / 지금 쓰는 %d개 / 끊김=%s"),
				TourCandidates.Num(), TourViewpoints.Num(),
				bTourSkipped ? TEXT("예") : TEXT("아니오"));

			UE_LOG(LogFestaFiestaFinale, Display,
				TEXT("  3막=%s 지금 막=%s 1막 몫=%d 발사 시계=%.1fs"),
				bActMode ? TEXT("예") : TEXT("아니오"),
				*UEnum::GetDisplayValueAsText(CurrentAct).ToString(),
				ActOneQuota, FiringClock);
		}));
}

void UFF_FinaleShowSubsystem::UnregisterDebugConsoleCommands()
{
	// 내가 건 것 중 아직 내가 주인인 것만 지운다. 다른 월드가 이미 지운 객체를 다시 지우지 않는다.
	DebugCommands.UnregisterAll();
}

#endif // !UE_BUILD_SHIPPING

AFF_FestivalPlayerController* UFF_FinaleShowSubsystem::GetFestivalController() const
{
	const UWorld* World = GetWorld();
	return World ? Cast<AFF_FestivalPlayerController>(World->GetFirstPlayerController()) : nullptr;
}

const UFF_FinaleShowData* UFF_FinaleShowSubsystem::ResolveShowData() const
{
	// 1. 코드에서 직접 꽂은 것. 연출을 임시로 바꿔볼 때 쓴다.
	if (const UFF_FinaleShowData* Direct = ShowData.LoadSynchronous())
	{
		return Direct;
	}

	// 2. 이 레벨의 GameMode 지정. 연등축제처럼 연출이 다른 레벨에서 쓴다.
	const UWorld* World = GetWorld();
	if (const AFF_FestivalGameMode* GameMode =
		World ? Cast<AFF_FestivalGameMode>(World->GetAuthGameMode()) : nullptr)
	{
		if (const UFF_FinaleShowData* FromLevel = GameMode->FinaleShowDataOverride.LoadSynchronous())
		{
			return FromLevel;
		}
	}

	// 3. 프로젝트 기본값. 대부분의 레벨은 여기까지 온다.
	if (const UFF_FinaleSettings* Settings = UFF_FinaleSettings::Get())
	{
		if (const UFF_FinaleShowData* FromSettings = Settings->DefaultShowData.LoadSynchronous())
		{
			return FromSettings;
		}
	}

	return nullptr;
}

FFF_FinaleGradeSpec UFF_FinaleShowSubsystem::ResolveSpec(const EFF_FinaleGrade Grade) const
{
	// 데이터 애셋이 있으면 그쪽 값이 이긴다. 없으면 코드 기본값이다.
	if (const UFF_FinaleShowData* Data = ResolveShowData())
	{
		return Data->GetSpec(Grade);
	}
	return FFF_FinaleDefaults::MakeSpec(Grade);
}

bool UFF_FinaleShowSubsystem::StartShow()
{
	EFF_FinaleGrade Grade = EFF_FinaleGrade::None;

	// 등급은 축제 진행 쪽이 소유한다. 여기서는 읽기만 한다.
	if (const UFF_FestivalProgressionSubsystem* Progression =
		GetGameInstanceSubsystem<UFF_FestivalProgressionSubsystem>())
	{
		Grade = Progression->GetPurchasedFinaleGrade();
	}
	else
	{
		// 진행 시스템이 없는 테스트 맵이라면 미구매 연출로 돈다.
		UE_LOG(LogFestaFiestaFinale, Log,
			TEXT("[피날레] 축제 진행 시스템이 없어 미구매 등급으로 진행한다."));
	}

	return StartShowWithGrade(Grade);
}

bool UFF_FinaleShowSubsystem::StartShowWithGrade(const EFF_FinaleGrade Grade)
{
	if (bShowInProgress)
	{
		UE_LOG(LogFestaFiestaFinale, Warning, TEXT("[피날레] 이미 연출이 진행 중이다."));
		return false;
	}

	ActiveSpec = ResolveSpec(Grade);
	if (!ActiveSpec.IsPlayable())
	{
		// 미구매는 원래 안 쏜다(FFF_FinaleDefaults 주석). 그건 경고할 일이 아니다.
		if (Grade == EFF_FinaleGrade::None)
		{
			UE_LOG(LogFestaFiestaFinale, Log, TEXT("[피날레] 미구매라 불꽃을 쏘지 않는다."));
		}
		else
		{
			UE_LOG(LogFestaFiestaFinale, Warning,
				TEXT("[피날레] 등급 %d 의 연출 수치가 비어 있어 시작하지 않는다."), static_cast<int32>(Grade));
		}
		return false;
	}

	ActiveGrade = Grade;
	bShowInProgress = true;
	ElapsedSeconds = 0.f;
	FiredBursts = 0;
	LaunchedShells = 0;
	PendingShells.Reset();
	InFlightShells.Reset();
	InFlightBurstCount = 0;
	ActiveLaunch = ResolveLaunchSpec();
	bWarnedHighShellCount = false;
	FiredVolleys = 0;
	ColorCursor = 0;
	TailStartedAt = -1.f;
	bWarnedMissingEffect = false;
	bWarnedDeadline = false;
	FiringClock = 0.f;

	// 연출을 새로 시작하면 검사도 다시 한다. 데이터 애셋을 고치고 재생했을 때
	// 바뀐 이펙트를 그대로 통과시키면 안 된다.
	VerifiedEffect = nullptr;

	// 첫 발은 바로 쏜다. 간격만큼 기다리면 시점 전환 뒤 정적이 생긴다.
	NextVolleyAt = 0.f;

	// 레벨에 발사 지점·시점이 없으면 데이터의 기본 배치를 세운다. 순회 목록을 만들기 전에 해야 잡힌다.
	EnsureFallbackLayout();

	BuildTour();

	// 1막 몫. 3막이 아니면 마무리 몫이 0 이라 전부가 된다.
	ActOneQuota = ActiveSpec.BurstCount - (bActMode ? ActiveSpec.GetGrandFinaleReserve() : 0);
	CurrentAct = (bActMode && TourActs.Num() > 0) ? TourActs[0] : EFF_FinaleTourAct::Fireworks;

	if (bTakeOverCamera)
	{
		// 시점이 없어도 연출은 진행한다. 카메라가 안 옮겨질 뿐 불꽃은 터져야 한다.
		ApplyTourViewpoint(CameraBlendSeconds);
	}

	UE_LOG(LogFestaFiestaFinale, Log,
		TEXT("[피날레] 연출 시작. 등급=%d 발수=%d 지속=%.1fs 동시=%d 3막=%s"),
		static_cast<int32>(Grade), ActiveSpec.BurstCount, ActiveSpec.DurationSeconds,
		ActiveSpec.SimultaneousShells, bActMode ? TEXT("예") : TEXT("아니오"));

	OnShowStateChanged.Broadcast(true);

	// 첫 막도 알린다. 받는 쪽이 시작 상태를 따로 추측하지 않게 한다.
	if (bShowInProgress && bActMode)
	{
		OnTourActChanged.Broadcast(CurrentAct);
	}
	return true;
}

void UFF_FinaleShowSubsystem::StopShow()
{
	if (!bShowInProgress)
	{
		return;
	}

	bShowInProgress = false;

	// 예약된 발사는 버린다. 연출이 끝난 뒤에 뒤늦게 터지면 이상하다.
	// 오르던 발도 같다. 꼬리는 바로 지운다.
	PendingShells.Reset();
	for (const FInFlightShell& Shell : InFlightShells)
	{
		if (AFF_FinaleShellTracer* Tracer = Shell.Tracer.Get())
		{
			Tracer->Discard();
		}
	}
	InFlightShells.Reset();
	InFlightBurstCount = 0;

	/* 순회 목록도 비운다.

	   남겨두면 IsCinematicTourActive 가 연출이 끝난 뒤에도 true 를 돌려줘서
	   `카메라 건너뛰기` 안내가 계속 떠 있다. 시점 액터를 강참조로 붙잡고 있기도 하다.
	*/
	TourViewpoints.Reset();
	TourActs.Reset();
	TourIndex = 0;
	TourHeldSeconds = 0.f;
	bActMode = false;
	CurrentAct = EFF_FinaleTourAct::Fireworks;

	if (AFF_FestivalPlayerController* Controller = GetFestivalController())
	{
		if (Controller->IsInScriptedView())
		{
			Controller->ExitScriptedView(CameraBlendSeconds);
		}
	}

	UE_LOG(LogFestaFiestaFinale, Log,
		TEXT("[피날레] 연출 종료. 총 %d 발, %.1f 초"), FiredBursts, ElapsedSeconds);

	OnShowStateChanged.Broadcast(false);
}

void UFF_FinaleShowSubsystem::Tick(float DeltaTime)
{
	if (!bShowInProgress || DeltaTime <= 0.f)
	{
		return;
	}

	ElapsedSeconds += DeltaTime;

	/* 지연 발사를 먼저 처리한다.

	   뒤에서 처리하면 이번 프레임에 예약된 것이 한 프레임 늦게 나간다.
	   일렬 패턴의 간격이 0.05초 단위라 한 프레임 차이가 눈에 보인다.

	   때가 된 것을 먼저 빼낸 뒤에 쏜다. 쏘면 OnBurst 가 울리는데, 받는 쪽이 연출을
	   멈추면 StopShow 가 대기열을 비운다. 대기열을 돌면서 쏘면 그 순간 인덱스가
	   배열 밖을 가리킨다.
	*/
	TArray<FPendingShell> DueShells;
	for (int32 Index = PendingShells.Num() - 1; Index >= 0; --Index)
	{
		FPendingShell& Pending = PendingShells[Index];
		Pending.DelayRemaining -= DeltaTime;

		if (Pending.DelayRemaining <= 0.f)
		{
			DueShells.Add(Pending);
			PendingShells.RemoveAtSwap(Index);
		}
	}

	for (const FPendingShell& Due : DueShells)
	{
		if (!bShowInProgress)
		{
			return;
		}
		LaunchShell(Due.Origin, Due.Location, Due.Color, Due.Effect.Get(), Due.SizeScale, Due.RiseSeconds);
	}

	if (!bShowInProgress)
	{
		return;
	}

	// 오르던 발을 옮기고 다 오른 발을 터뜨린다. 터지는 신호를 받은 쪽이 연출을 멈출 수 있다.
	if (!TickInFlightShells(DeltaTime))
	{
		return;
	}

	/* 카메라 순회. 자리마다 정한 시간만큼 머물고 다음으로 넘어간다.

	   스킵했으면 더 움직이지 않는다. 플레이어가 끊은 뒤에 다시 끌고 가면
	   끊은 의미가 없다. 카메라를 넘겨받지 않는 연출이면 처음부터 돌지 않는다 —
	   안 그러면 플레이어가 보던 시점을 순회가 빼앗아 간다.
	*/
	if (bTakeOverCamera && !bTourSkipped && TourViewpoints.Num() > 1)
	{
		TourHeldSeconds += DeltaTime;

		const AFF_FinaleViewpoint* Current = TourViewpoints.IsValidIndex(TourIndex)
			? TourViewpoints[TourIndex].Get()
			: nullptr;

		// 축제장 순회 카메라는 머무는 동안 경로를 따라 날아간다.
		if (Current && Current == SiteTourCamera)
		{
			UpdateSiteTourCamera(TourHeldSeconds, false);
		}
		const float Hold = Current ? FMath::Max(0.5f, Current->TourHoldSeconds) : 6.f;

		// 3막의 마지막 자리(마무리)에서는 넘어가지 않고 끝까지 머문다.
		const bool bAtFinalSeat = bActMode && TourIndex >= TourViewpoints.Num() - 1;

		if (TourHeldSeconds >= Hold && !bAtFinalSeat)
		{
			AdvanceTour();

			// 막이 바뀌었다는 신호를 받은 쪽이 연출을 멈췄을 수 있다.
			if (!bShowInProgress)
			{
				return;
			}
		}
	}

	/* 아직 쏠 게 남았으면 간격을 채울 때마다 한 차례씩 쏜다.

	   끝은 차례 수가 아니라 남은 발수로 정한다. 차례 수는 기댓값이라(GetVolleyCount)
	   그걸로 끊으면 발이 남았는데 끝나거나, 발이 동났는데 빈 차례가 계속 돈다.

	   다만 무한히 돌지는 않게 시간으로 한 번 더 막는다. 모든 차례가 0발을 내는
	   설정(예: 모양 애셋이 전부 비었는데 흩뿌림도 막힌 경우)이 생겨도 연출은 끝나야 한다.
	*/
	const bool bGateOpen = IsFiringGateOpen();
	if (bGateOpen || PendingShells.Num() > 0)
	{
		FiringClock += DeltaTime;
	}

	// 상한은 발사 시계로 잰다. 전체 경과 시간으로 재면 축제장 순회가 길 때 마무리 막에서 잘린다.
	const float FiringDeadline = FMath::Max(ActiveSpec.DurationSeconds, 1.f) * 2.f;
	const bool bPastDeadline = FiringClock >= FiringDeadline;

	if (bPastDeadline && GetRemainingShells() > 0 && !bWarnedDeadline)
	{
		bWarnedDeadline = true;
		UE_LOG(LogFestaFiestaFinale, Warning,
			TEXT("[피날레] 지속 시간의 두 배가 지나도록 %d발이 남아 발사를 멈춘다. 볼리 설정을 볼 것."),
			GetRemainingShells());
	}

	if (!bPastDeadline && GetRemainingShells() > 0)
	{
		// 막이 발사를 막고 있다(축제장 순회 중이거나 1막 몫을 다 쐈다). 남은 발이 있으니 끝내지 않는다.
		if (!bGateOpen)
		{
			return;
		}

		/* 마무리 막의 마지막 한 방.

		   예약된 발이 다 나간 뒤에 쏜다. 일렬 패턴이 아직 날아가는 중에 일제 발사가
		   겹치면 마지막이 어수선하다. 간격도 지킨다. 바로 앞 차례와 붙어 나가면 한 방으로 안 보인다.
		   오르는 중인 발은 기다리지 않는다. 일제 발사도 오르는 데 몇 초 걸리므로, 앞서 오른 발이
		   다 터지기를 기다리면 하늘이 비었다가 터진다. 실제 마무리도 앞 발이 터지는 사이에 올린다.
		*/
		const bool bFinalSalvoDue = bActMode
			&& CurrentAct == EFF_FinaleTourAct::GrandFinale
			&& ActiveSpec.FinalSalvoShells > 0
			&& GetRemainingShells() <= ActiveSpec.FinalSalvoShells
			&& PendingShells.Num() == 0
			&& FiringClock >= NextVolleyAt;

		if (bFinalSalvoDue)
		{
			FireFinalSalvo();
			return;
		}

		const float Interval = ActiveSpec.GetIntervalSeconds() * GetActIntervalScale();

		// 프레임이 크게 밀렸을 때 밀린 만큼 한꺼번에 쏘면 하늘이 갑자기 꽉 찬다.
		// 한 프레임에 최대 두 차례까지만 따라잡는다.
		// 루프 안에서도 문을 다시 본다. 1막 몫을 채우는 순간 멈춰야 마무리 몫이 남는다.
		int32 CatchUp = 0;
		while (bShowInProgress && FiringClock >= NextVolleyAt
			&& IsFiringGateOpen() && CatchUp < 2)
		{
			FireVolley();
			NextVolleyAt += Interval;
			++CatchUp;
		}

		if (!bShowInProgress)
		{
			return;
		}

		// 따라잡기를 포기했으면 다음 기준을 현재 시각으로 당긴다.
		if (CatchUp >= 2 && FiringClock > NextVolleyAt)
		{
			NextVolleyAt = FiringClock + Interval;
		}
		return;
	}

	// 3막은 마무리 막에 닿기 전에는 끝내지 않는다. 상한에 걸려도 카메라가 마무리를 보여주고 끝난다.
	if (bActMode && CurrentAct != EFF_FinaleTourAct::GrandFinale)
	{
		return;
	}

	// 지연 발사나 오르는 발이 남아 있으면 기다린다. 여운을 먼저 세면 마지막 발들이 잘린다.
	// 오르는 발은 상한을 넘겨도 기다린다. 이미 하늘에 떠 있는 알이 안 터지고 사라지면 이상하다.
	if ((PendingShells.Num() > 0 && !bPastDeadline) || InFlightShells.Num() > 0)
	{
		return;
	}

	// 다 쐈으면 여운을 두고 시점을 되돌린다.
	// 마지막 발이 터지자마자 화면이 바뀌면 그 불꽃이 잘린다.
	if (TailStartedAt < 0.f)
	{
		TailStartedAt = ElapsedSeconds;
		return;
	}

	if (ElapsedSeconds - TailStartedAt >= TailSeconds)
	{
		StopShow();
	}
}

void UFF_FinaleShowSubsystem::CollectLaunchPoints(TArray<FVector>& OutPoints) const
{
	AFF_FinaleLaunchSite::CollectLaunchPoints(GetWorld(), OutPoints);
	if (OutPoints.Num() == 0)
	{
		// 레벨이 아직 없는 단계다. 원점 위에서 터뜨리고 연출 자체는 굴린다.
		OutPoints.Add(FVector::ZeroVector);
	}
}

FVector UFF_FinaleShowSubsystem::ResolveLaunchOrigin() const
{
	// 위치가 여럿이면 차례로 돌아간다. 바지선 3척이면 왼쪽, 가운데, 오른쪽 순이다.
	TArray<FVector> Points;
	CollectLaunchPoints(Points);
	return Points[FiredVolleys % Points.Num()];
}

FVector UFF_FinaleShowSubsystem::PickBurstLocation(const FVector& Origin) const
{
	const float Radius = FMath::Max(0.f, ActiveSpec.SpreadRadius);

	// 원 안에 고르게 흩는다. 반지름을 그냥 균등 추출하면 가운데가 빽빽해진다.
	const float Angle = ShowRandom.FRandRange(0.f, 2.f * PI);
	const float Distance = Radius * FMath::Sqrt(ShowRandom.FRand());

	const float Height = ShowRandom.FRandRange(
		FMath::Min(ActiveSpec.BurstHeightMin, ActiveSpec.BurstHeightMax),
		FMath::Max(ActiveSpec.BurstHeightMin, ActiveSpec.BurstHeightMax));

	return Origin + FVector(
		FMath::Cos(Angle) * Distance,
		FMath::Sin(Angle) * Distance,
		Height);
}

FVector UFF_FinaleShowSubsystem::ResolveAudienceDirection(const FVector& Center) const
{
	/* 모양이 마주봐야 할 방향. 중심에서 보는 쪽으로 향한다.

	   순회 중인 시점을 먼저 본다. 카메라가 거기 있으니 그게 곧 관객의 눈이다.
	   순회를 끊었거나 시점이 없으면 플레이어 카메라를 본다.

	   둘 다 없으면(테스트 맵, 콘솔 재생) 기본축을 돌려준다. 방향을 모를 때
	   모양을 안 그리는 것보다, 한 방향으로라도 세우는 편이 낫다.
	*/
	// 순회 시점은 카메라가 실제로 거기 있을 때만 믿는다. 끊었거나 넘겨받지 않았으면
	// 플레이어는 자기 카메라로 보고 있다.
	const bool bViewingFromTour = bTakeOverCamera && !bTourSkipped;

	if (bViewingFromTour && TourViewpoints.IsValidIndex(TourIndex))
	{
		if (const AFF_FinaleViewpoint* Viewpoint = TourViewpoints[TourIndex])
		{
			const FVector Direction = (Viewpoint->GetActorLocation() - Center).GetSafeNormal2D();
			if (!Direction.IsNearlyZero())
			{
				return Direction;
			}
		}
	}

	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* Controller = World->GetFirstPlayerController())
		{
			FVector ViewLocation = FVector::ZeroVector;
			FRotator ViewRotation = FRotator::ZeroRotator;
			Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

			const FVector Direction = (ViewLocation - Center).GetSafeNormal2D();
			if (!Direction.IsNearlyZero())
			{
				return Direction;
			}
		}
	}

	return FVector::ForwardVector;
}

FLinearColor UFF_FinaleShowSubsystem::PickColor(const int32 Index) const
{
	if (ActiveSpec.Colors.Num() == 0)
	{
		return FLinearColor::White;
	}
	return ActiveSpec.Colors[Index % ActiveSpec.Colors.Num()];
}

void UFF_FinaleShowSubsystem::VerifyEffectParameters(const UNiagaraSystem& Effect) const
{
	// 주의: 이 함수만 Niagara 내부 파라미터 조회에 의존한다.
	// 엔진 버전에 따라 조회 API 이름이 다를 수 있는데, 그때는 이 함수 본문만
	// 비워도 나머지 연출은 그대로 돈다. 값 전달과 재생 경로는 여기에 얽혀 있지 않다.
	const FNiagaraUserRedirectionParameterStore& Store = Effect.GetExposedParameters();

	TArray<FNiagaraVariable> Exposed;
	Store.GetParameters(Exposed);

	// 이펙트가 실제로 노출한 이름을 모은다. User. 접두사가 붙어 오는 경우가 있어 양쪽을 다 담는다.
	TSet<FName> Available;
	for (const FNiagaraVariable& Variable : Exposed)
	{
		const FString Full = Variable.GetName().ToString();
		Available.Add(Variable.GetName());

		FString Trimmed;
		if (Full.Split(TEXT("."), nullptr, &Trimmed, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			Available.Add(FName(*Trimmed));
		}
	}

	const TArray<FName> Required = {
		FFF_FinaleParams::BurstColor,
		FFF_FinaleParams::BurstScale,
		FFF_FinaleParams::TrailLength
	};

	TArray<FString> Missing;
	for (const FName& Name : Required)
	{
		if (!Available.Contains(Name))
		{
			Missing.Add(Name.ToString());
		}
	}

	if (Missing.Num() == 0)
	{
		UE_LOG(LogFestaFiestaFinale, Verbose,
			TEXT("[피날레] 이펙트 %s 의 파라미터가 모두 연결되었다."), *Effect.GetName());
		return;
	}

	// 없어도 연출은 계속한다. 색이 안 먹을 뿐 불꽃은 터져야 한다.
	UE_LOG(LogFestaFiestaFinale, Warning,
		TEXT("[피날레] 이펙트 %s 에 User 파라미터가 없어 값이 무시된다: %s"),
		*Effect.GetName(), *FString::Join(Missing, TEXT(", ")));
	UE_LOG(LogFestaFiestaFinale, Warning,
		TEXT("[피날레] Niagara 에서 같은 이름으로 User 파라미터를 만들면 등급별 값이 반영된다."));
}

void UFF_FinaleShowSubsystem::BuildTour()
{
	TourViewpoints.Reset();
	TourActs.Reset();
	TourIndex = 0;
	TourHeldSeconds = 0.f;
	bTourSkipped = false;
	bActMode = false;

	TArray<AFF_FinaleViewpoint*> Candidates;
	AFF_FinaleViewpoint::CollectTour(GetWorld(), Candidates);

	// 막별로 나눈다. CollectTour 가 TourOrder 로 정렬해 두었으므로 막 안의 순서도 그대로 간다.
	TArray<AFF_FinaleViewpoint*> SkySeats;
	TArray<AFF_FinaleViewpoint*> SiteSeats;
	TArray<AFF_FinaleViewpoint*> FinaleSeats;
	for (AFF_FinaleViewpoint* Candidate : Candidates)
	{
		switch (Candidate->TourAct)
		{
		case EFF_FinaleTourAct::SiteTour:    SiteSeats.Add(Candidate);   break;
		case EFF_FinaleTourAct::GrandFinale: FinaleSeats.Add(Candidate); break;
		default:                             SkySeats.Add(Candidate);    break;
		}
	}

	/* 3막은 카메라를 넘겨받을 때만 선다.

	   막을 넘기는 건 카메라가 머문 시간이다. 카메라를 안 넘겨받으면 축제장 순회가
	   화면에 안 나오는데 발사만 멈춰서, 플레이어는 이유 없이 빈 하늘을 보게 된다.
	*/
	const bool bWantActs = ActiveSpec.bSiteTour && bTakeOverCamera;

	/* 축제장 순회는 플레이어가 지은 시설을 따라 날아가는 카메라가 먼저다(FFF_FinaleSiteTourSpec).

	   입구도 시설도 없을 때만 레벨·데이터에 놓인 고정 순회 자리로 물러난다.
	*/
	const bool bFollowFacilities = bWantActs && SkySeats.Num() > 0 && PrepareSiteTour();
	if (bFollowFacilities)
	{
		SiteSeats.Reset();
		SiteSeats.Add(SiteTourCamera.Get());
	}

	if (bWantActs && SkySeats.Num() > 0 && SiteSeats.Num() > 0)
	{
		for (AFF_FinaleViewpoint* Seat : SkySeats)
		{
			TourViewpoints.Add(Seat);
			TourActs.Add(EFF_FinaleTourAct::Fireworks);
		}
		for (AFF_FinaleViewpoint* Seat : SiteSeats)
		{
			TourViewpoints.Add(Seat);
			TourActs.Add(EFF_FinaleTourAct::SiteTour);
		}

		// 마무리 자리가 없으면 처음 불꽃을 본 자리로 돌아온다. 같은 구도로 끝나야 "다시 본다" 가 된다.
		if (FinaleSeats.Num() == 0)
		{
			FinaleSeats.Add(SkySeats[0]);
		}
		for (AFF_FinaleViewpoint* Seat : FinaleSeats)
		{
			TourViewpoints.Add(Seat);
			TourActs.Add(EFF_FinaleTourAct::GrandFinale);
		}

		bActMode = true;
		UE_LOG(LogFestaFiestaFinale, Log,
			TEXT("[피날레] 3막으로 진행한다. 불꽃 관람 %d / 축제장 순회 %s / 마무리 %d"),
			SkySeats.Num(),
			*(bFollowFacilities
				? FString::Printf(TEXT("시설 따라 %d곳 %.0fm %.1f초"),
					SiteTourRoute.Stops.Num() - 1, SiteTourRoute.GetLength() / 100.f, SiteTourSeconds)
				: FString::Printf(TEXT("고정 자리 %d"), SiteSeats.Num())),
			FinaleSeats.Num());
		return;
	}

	if (bWantActs)
	{
		// 켜놨는데 막이 안 섰다. 레벨 배치 문제라 알려야 한다. 조용히 넘어가면 등급 차이가 사라진다.
		UE_LOG(LogFestaFiestaFinale, Warning,
			TEXT("[피날레] 3막을 켰지만 시점이 모자라 하늘 시점만 돈다. 불꽃 관람 %d / 축제장 순회 %d (둘 다 1개 이상 필요)"),
			SkySeats.Num(), SiteSeats.Num());
	}

	// 3막이 아니면 하늘 시점만 쓴다. 불꽃이 터지는 동안 땅을 보고 있으면 안 된다.
	TArray<AFF_FinaleViewpoint*> SkyOnly;
	for (AFF_FinaleViewpoint* Candidate : Candidates)
	{
		if (Candidate->TourAct != EFF_FinaleTourAct::SiteTour)
		{
			SkyOnly.Add(Candidate);
		}
	}

	if (SkyOnly.Num() == 0)
	{
		// 순회용으로 표시된 시점이 없다. 고정 연출로 쓸 자리라도 찾아본다.
		if (AFF_FinaleViewpoint* Best = AFF_FinaleViewpoint::FindBest(GetWorld()))
		{
			TourViewpoints.Add(Best);
			TourActs.Add(EFF_FinaleTourAct::Fireworks);
		}
		return;
	}

	// 0 이면 제한 없음. 등급이 낮으면 앞에서부터 몇 개만 쓴다.
	const int32 Limit = ActiveSpec.TourViewpointLimit;
	const int32 Take = (Limit <= 0) ? SkyOnly.Num() : FMath::Min(Limit, SkyOnly.Num());

	for (int32 Index = 0; Index < Take; ++Index)
	{
		TourViewpoints.Add(SkyOnly[Index]);
		TourActs.Add(EFF_FinaleTourAct::Fireworks);
	}

	UE_LOG(LogFestaFiestaFinale, Log,
		TEXT("[피날레] 시점 %d개 중 %d개를 쓴다. (제한 %d)"),
		SkyOnly.Num(), TourViewpoints.Num(), Limit);
}

void UFF_FinaleShowSubsystem::ApplyTourViewpoint(const float BlendSeconds)
{
	AFF_FestivalPlayerController* Controller = GetFestivalController();
	if (!Controller)
	{
		return;
	}

	AFF_FinaleViewpoint* Viewpoint = TourViewpoints.IsValidIndex(TourIndex)
		? TourViewpoints[TourIndex].Get()
		: nullptr;

	// nullptr 을 넘기면 컨트롤러가 알아서 가장 나은 자리를 고른다.
	Controller->EnterScriptedView(Viewpoint, BlendSeconds);

	// 머무는 시간 동안 흘러가게 한다. 이동량이 0 인 자리는 그대로 서 있다.
	if (Viewpoint)
	{
		Viewpoint->BeginTourDrift(FMath::Max(0.5f, Viewpoint->TourHoldSeconds));
	}
}

void UFF_FinaleShowSubsystem::AdvanceTour()
{
	if (TourViewpoints.Num() <= 1)
	{
		return;
	}

	// 3막은 한 번만 지나간다. 마무리에서 첫 자리로 돌아가면 끝난 줄 알았던 연출이 다시 시작한다.
	TourIndex = bActMode
		? FMath::Min(TourIndex + 1, TourViewpoints.Num() - 1)
		: (TourIndex + 1) % TourViewpoints.Num();
	TourHeldSeconds = 0.f;

	// 다음 자리가 정한 전환 시간을 쓴다. 자리마다 컷으로 자를지 흐르게 할지 다르다.
	const AFF_FinaleViewpoint* Next = TourViewpoints[TourIndex].Get();
	const float Blend = Next ? Next->TourBlendSeconds : CameraBlendSeconds;

	ApplyTourViewpoint(Blend);

	if (bActMode && TourActs.IsValidIndex(TourIndex))
	{
		SetCurrentAct(TourActs[TourIndex]);
	}
}

void UFF_FinaleShowSubsystem::SetCurrentAct(const EFF_FinaleTourAct NewAct)
{
	if (CurrentAct == NewAct)
	{
		return;
	}

	CurrentAct = NewAct;
	UE_LOG(LogFestaFiestaFinale, Log, TEXT("[피날레] 막 전환: %s (발사 %d/%d)"),
		*UEnum::GetDisplayValueAsText(NewAct).ToString(), FiredBursts, ActiveSpec.BurstCount);

	OnTourActChanged.Broadcast(NewAct);
}

bool UFF_FinaleShowSubsystem::IsFiringGateOpen() const
{
	if (GetRemainingShells() <= 0)
	{
		return false;
	}

	if (!bActMode)
	{
		return true;
	}

	switch (CurrentAct)
	{
	case EFF_FinaleTourAct::SiteTour:
		// 축제장을 보는 동안 하늘에서 터져봐야 화면에 없다. 그 발은 마무리에 쓴다.
		return false;

	case EFF_FinaleTourAct::Fireworks:
		// 예약된 발과 오르는 발까지 센다. 빼먹으면 날아가는 동안 몫을 넘겨 또 쏜다.
		return FiredBursts + PendingShells.Num() + InFlightBurstCount < ActOneQuota;

	default:
		return true;
	}
}

float UFF_FinaleShowSubsystem::GetActIntervalScale() const
{
	if (bActMode && CurrentAct == EFF_FinaleTourAct::GrandFinale)
	{
		return FMath::Clamp(ActiveSpec.GrandFinaleIntervalScale, 0.1f, 1.f);
	}
	return 1.f;
}

void UFF_FinaleShowSubsystem::FireFinalSalvo()
{
	const int32 ShellCount = GetRemainingShells();
	if (ShellCount <= 0)
	{
		return;
	}

	/* 발사대가 여럿이면 전부에서 동시에 올린다.

	   평소에는 차례마다 발사대를 번갈아 쓰는데(ResolveLaunchOrigin), 마지막까지 한 곳에서만
	   올리면 일제 발사가 한 덩어리로 뭉친다. 하늘 전체가 한꺼번에 차야 끝난다는 게 읽힌다.
	*/
	// 바지선까지 펼친 위치를 쓴다. 실제 해상 불꽃처럼 모든 배에서 한꺼번에 올라가야 하늘 폭이 넓게 찬다.
	TArray<FVector> Points;
	CollectLaunchPoints(Points);

	UE_LOG(LogFestaFiestaFinale, Log, TEXT("[피날레] 일제 발사 %d발 (발사 위치 %d곳)"), ShellCount, Points.Num());

	for (int32 Shell = 0; Shell < ShellCount; ++Shell)
	{
		const FVector& Origin = Points[Shell % Points.Num()];
		const FVector Location = PickBurstLocation(Origin);

		// 높이마다 상승 시간이 달라 한꺼번에 올라가도 차례로 터진다. 실제 일제 발사도 그렇다.
		LaunchShell(Origin, Location, PickColor(ColorCursor++),
			ActiveSpec.PickEffect(ShowRandom.GetUnsignedInt()),
			ActiveSpec.ResolveCaliberScale(Location.Z - Origin.Z));

		// OnBurst 를 받은 쪽이 연출을 멈췄다. 남은 발을 쏘지 않는다.
		if (!bShowInProgress)
		{
			return;
		}
	}

	++FiredVolleys;
}

void UFF_FinaleShowSubsystem::SkipCinematicCamera()
{
	if (bTourSkipped || TourViewpoints.Num() <= 1)
	{
		return;
	}

	bTourSkipped = true;

	/* 3막이면 곧장 마무리 막으로 넘긴다.

	   막을 넘기는 건 카메라가 머문 시간인데, 끊으면 카메라가 더 움직이지 않는다.
	   축제장 순회 중에 끊으면 발사가 닫힌 채로 남아 연출이 끝나지 않는다.
	*/
	if (bActMode)
	{
		TourIndex = TourViewpoints.Num() - 1;
		SetCurrentAct(EFF_FinaleTourAct::GrandFinale);

		if (!bShowInProgress)
		{
			return;
		}
	}

	// 카메라만 놓아준다. 연출은 그대로 둔다.
	if (AFF_FestivalPlayerController* Controller = GetFestivalController())
	{
		if (Controller->IsInScriptedView())
		{
			Controller->ExitScriptedView(CameraBlendSeconds);
		}
	}

	UE_LOG(LogFestaFiestaFinale, Log, TEXT("[피날레] 카메라 순회를 끊었다. 불꽃은 계속된다."));
}

const FFF_FinaleVolley& UFF_FinaleShowSubsystem::PickVolley(const int32 RemainingShells) const
{
	// 패턴이 없으면 단발이다. 정적 객체를 돌려줘 호출부가 널 검사를 안 하게 한다.
	static const FFF_FinaleVolley SingleShot;

	if (ActiveSpec.Volleys.Num() == 0)
	{
		return SingleShot;
	}

	/* 남은 발수로 온전히 그릴 수 있는 것만 후보로 둔다.

	   모양이 있는 패턴은 발수가 곧 모양이다. 12발짜리 원을 3발로 줄이면 원이 아니라
	   점 세 개가 되고, 연출의 마지막이 그 꼴로 끝난다. 발수를 깎는 것보다
	   그 차례에 단발을 쏘는 편이 낫다.

	   자리를 안 정하는 패턴(단발·동시)은 발수가 줄어도 그냥 덜 터질 뿐이라 거른다.
	*/
	/* 진행률이 안 된 패턴도 거른다(ShowProgressFrom). 처음엔 기본 불꽃, 끝으로 갈수록 큰 모양이다.

	   진행률은 쏜 발수(오르는 발 포함)로 잰다. 시간으로 재면 3막의 축제장 순회 동안 시간만 흘러
	   마무리 막에 들어서자마자 큰 모양이 몰린다.
	*/
	const float Progress = ActiveSpec.BurstCount > 0
		? 1.f - static_cast<float>(RemainingShells) / static_cast<float>(ActiveSpec.BurstCount)
		: 1.f;
	auto Fits = [RemainingShells, Progress](const FFF_FinaleVolley& Volley)
	{
		return Volley.IsAvailableAt(Progress)
			&& (!Volley.HasFixedLayout() || Volley.ShellCount <= RemainingShells);
	};

	float Total = 0.f;
	for (const FFF_FinaleVolley& Volley : ActiveSpec.Volleys)
	{
		if (Fits(Volley))
		{
			Total += FMath::Max(0.01f, Volley.Weight);
		}
	}

	if (Total <= 0.f)
	{
		// 남은 발수로 그릴 수 있는 모양이 없다. 남은 것은 단발로 채운다.
		return SingleShot;
	}

	// 가중치로 뽑는다. 같은 패턴이 계속 나오면 연출이 단조로워진다.
	float Roll = ShowRandom.FRandRange(0.f, Total);
	for (const FFF_FinaleVolley& Volley : ActiveSpec.Volleys)
	{
		if (!Fits(Volley))
		{
			continue;
		}

		Roll -= FMath::Max(0.01f, Volley.Weight);
		if (Roll <= 0.f)
		{
			return Volley;
		}
	}

	// 부동소수 오차로 여기까지 오면 마지막으로 통과한 것을 쓴다.
	for (int32 Index = ActiveSpec.Volleys.Num() - 1; Index >= 0; --Index)
	{
		if (Fits(ActiveSpec.Volleys[Index]))
		{
			return ActiveSpec.Volleys[Index];
		}
	}

	return SingleShot;
}

void UFF_FinaleShowSubsystem::SpawnShell(
	const FVector& Location, const FLinearColor& Color, UNiagaraSystem* Effect, const float SizeScale)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 이펙트가 바뀌었을 때만 검사한다.
	//
	// 여러 종류를 섞어 쓰면 종류마다 한 번씩 돈다. 파라미터 이름이 빠진 이펙트가
	// 섞여 있으면 그것만 색이 안 바뀌는데, 검사를 첫 발에서만 하면 그걸 놓친다.
	if (Effect && VerifiedEffect.Get() != Effect)
	{
		VerifyEffectParameters(*Effect);
		VerifiedEffect = Effect;
	}

	if (Effect)
	{
		// 컴포넌트를 받아 파라미터를 넣는다. SpawnSystemAtLocation 자체는 값을 못 넘긴다.
		UNiagaraComponent* Spawned = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, Effect, Location);

		if (Spawned)
		{
			// 이펙트에 같은 이름의 User 파라미터가 없으면 조용히 무시된다.
			// 크래시가 안 나는 대신 "왜 색이 안 바뀌지" 로 나타나므로, 이름은 상수로만 쓴다.
			Spawned->SetColorParameter(FFF_FinaleParams::BurstColor, Color);
			Spawned->SetFloatParameter(FFF_FinaleParams::BurstScale, ActiveSpec.BurstScale * FMath::Max(0.1f, SizeScale));
			Spawned->SetFloatParameter(FFF_FinaleParams::TrailLength, ActiveSpec.TrailLength);
		}
	}
	else if (!bWarnedMissingEffect)
	{
		// 에셋이 아직 없는 단계다. 매 발 찍으면 로그가 넘치니 한 번만 알린다.
		// 어느 애셋의 몇 칸이 찼는지 같이 찍는다. 칸은 채웠는데 안 나올 때 원인이 여기서 갈린다.
		bWarnedMissingEffect = true;
		const UFF_FinaleShowData* Data = ResolveShowData();
		UE_LOG(LogFestaFiestaFinale, Warning,
			TEXT("[피날레] 이펙트 없이 위치 신호만 보낸다. 데이터=%s 등급=%d 이펙트 %s. "
				 "BurstEffects 를 확인할 것."),
			Data ? *Data->GetName() : TEXT("없음 (코드 기본값)"),
			static_cast<int32>(ActiveGrade), *DescribeEffects(ActiveSpec));
	}

	// 이펙트가 없어도 신호는 보낸다. 사운드나 화면 흔들림은 여기에 붙는다.
	OnBurst.Broadcast(Location, Color);
	++FiredBursts;
}

void UFF_FinaleShowSubsystem::FireVolley()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Origin = ResolveLaunchOrigin();

	/* 마지막 차례는 남은 발수만큼만 쏜다. 총 발수를 넘기면 등급 차이가 흐려진다.

	   예약만 되고 아직 안 터진 발도 뺀다. 빼지 않으면 줄지어 나가는 패턴 뒤에서
	   같은 발수를 또 쏴서 총 발수를 넘긴다.
	*/
	const int32 Remaining = GetRemainingShells();

	/* 남은 발수를 넘겨 고른다. 모양이 있는 패턴은 발수가 모자라면 모양이 깨진다.

	   참조가 아니라 복사로 든다. 아래에서 쏘면 OnBurst 가 울리는데, 받는 쪽이 연출을
	   멈추거나 다시 틀면 ActiveSpec 이 바뀌어 참조가 사라진 배열을 가리킨다.
	*/
	const FFF_FinaleVolley Volley = PickVolley(Remaining);

	const int32 Requested = FMath::Max(
		FMath::Max(1, ActiveSpec.SimultaneousShells), Volley.ShellCount);
	const int32 ShellCount = FMath::Min(Requested, Remaining);

	if (ShellCount <= 0)
	{
		++FiredVolleys;
		return;
	}

	/* 지나치게 많으면 알린다. 막지는 않는다.

	   화면을 보면서 계속 올리게 될 값이라 상한을 박으면 늘리고 싶을 때 걸린다.
	   대신 느려졌을 때 왜 느린지는 보여야 한다.
	*/
	if (!bWarnedHighShellCount && ShellCount > 64)
	{
		bWarnedHighShellCount = true;
		UE_LOG(LogFestaFiestaFinale, Warning,
			TEXT("[피날레] 한 차례에 %d 발이다. 프레임이 떨어지면 이 값부터 볼 것."), ShellCount);
	}

	// 모양이 있는 패턴은 색도 이펙트도 섞이면 모양이 안 보인다.
	const FLinearColor UniformColor = PickColor(ColorCursor++);
	UNiagaraSystem* UniformEffect = ActiveSpec.PickEffect(ShowRandom.GetUnsignedInt());

	/* 이번 차례에 쏠 자리를 먼저 다 구한다.

	   예전에는 루프 안에서 발마다 PickBurstLocation 을 불렀다. 그 함수는 부를 때마다
	   흩뿌림 반경 안에서 중심과 높이를 새로 뽑으므로, 모양이 있는 패턴은 발마다
	   다른 중심에서 오프셋돼 뭉개졌다. S 등급은 흩뿌림 반경 9000 에 모양 크기 10000
	   이라 원이 원으로 보이지 않았다.

	   그래서 모양이 있는 패턴은 중심을 여기서 한 번만 뽑아 넘긴다.
	*/
	TArray<FVector> ShellLocations;

	// 발마다 어느 위치에서 올라갔는지. 크기 배수를 그 위치 기준 높이로 정한다.
	TArray<FVector> ShellOrigins;

	/* 모양 패턴은 차례 전체가 같은 시간 동안 오른다. 0 이면 발마다 높이로 정한다.

	   모양은 발마다 높이가 다르다(수직면 하트는 위아래로 수십 m). 높이대로 시간을 주면
	   아래쪽이 먼저 터져 하트가 한꺼번에 피지 않는다. 실제 불꽃도 모양을 낼 때는 도화선을 맞춘다.
	*/
	float VolleyRiseSeconds = 0.f;

	// 모양을 제대로 그렸고 한꺼번에 피는 패턴이면 알 하나에 실어 올린다(LaunchCarrier).
	FVector PatternCenter = Origin;
	bool bPatternBuilt = false;

	if (Volley.HasFixedLayout())
	{
		FFF_FinaleLayoutContext Layout;
		Layout.Center = PickBurstLocation(Origin);
		PatternCenter = Layout.Center;
		VolleyRiseSeconds = ActiveLaunch.ResolveRiseSeconds(Layout.Center.Z - Origin.Z);
		Layout.FacingDirection = ResolveAudienceDirection(Layout.Center);
		Layout.ShellCount = ShellCount;

		// 바닥면 모양만 돌린다. 수직면은 돌리면 관객에게서 비껴서 읽히지 않는다.
		Layout.GroundYawDegrees = ShowRandom.FRandRange(0.f, 360.f);

		Volley.BuildShellLayout(Layout, ShellLocations);
		bPatternBuilt = ShellLocations.Num() > 0;

		/* 모양을 못 그렸으면(애셋이 비었거나 깨짐) 이번 차례는 흩뿌린다.

		   예전에는 이 차례를 건너뛰었다. 끝을 남은 발수로 정하게 바꾼 뒤로는 건너뛰면
		   발수가 안 줄어 같은 볼리가 계속 뽑히고 하늘이 빈다.
		*/
		if (ShellLocations.Num() == 0)
		{
			ShellLocations.Reserve(ShellCount);
			for (int32 Shell = 0; Shell < ShellCount; ++Shell)
			{
				ShellLocations.Add(PickBurstLocation(Origin));
			}
		}
	}
	else
	{
		/* 단발과 동시는 흩뿌린다. 발마다 따로 뽑는 것이 여기서는 의도다.

		   동시 발사는 발마다 다음 발사 위치로 넘긴다. 예전에는 한 차례를 한 곳에서만 올려서
		   바지선을 여럿 둬도 동시 발이 한 배 위에 뭉쳤다. 실제 해상 불꽃은 여러 배가 함께 쏴서
		   좌우로 넓게 찬다. 위치가 하나뿐이면 예전과 같다.
		*/
		TArray<FVector> Points;
		CollectLaunchPoints(Points);

		ShellLocations.Reserve(ShellCount);
		ShellOrigins.Reserve(ShellCount);
		for (int32 Shell = 0; Shell < ShellCount; ++Shell)
		{
			const FVector& ShellOrigin = Points[(FiredVolleys + Shell) % Points.Num()];
			ShellOrigins.Add(ShellOrigin);
			ShellLocations.Add(PickBurstLocation(ShellOrigin));
		}
	}

	/* 한 순간에 피는 모양(원·하트처럼 발 사이 간격이 0)은 알 하나가 올라가 터지며 그린다.

	   줄지어 나가는 일렬·부챗살(간격이 있는 것)은 실제로도 연발통에서 차례로 쏘므로 발마다 올린다.
	*/
	const bool bCarrier = bPatternBuilt && Volley.ShellDelaySeconds <= 0.f;
	TArray<FShellBurst> CarrierBursts;

	for (int32 Shell = 0; Shell < ShellLocations.Num(); ++Shell)
	{
		const FVector& Location = ShellLocations[Shell];

		const FLinearColor Color = Volley.bUniformColor ? UniformColor : PickColor(ColorCursor++);

		// 모양을 그리는 패턴은 이펙트도 하나로 맞춘다. 종류가 섞이면 모양이 안 보인다.
		UNiagaraSystem* Effect = Volley.HasFixedLayout()
			? UniformEffect
			: ActiveSpec.PickEffect(ShowRandom.GetUnsignedInt());

		// 모양 패턴은 크기를 맞춘다. 흩뿌린 발만 높이에 따라 구경을 바꾼다.
		const float SizeScale = (ShellOrigins.IsValidIndex(Shell)
			? ActiveSpec.ResolveCaliberScale(Location.Z - ShellOrigins[Shell].Z)
			: 1.f) * FMath::Max(0.05f, Volley.SizeMultiplier);

		if (bCarrier)
		{
			FShellBurst& Burst = CarrierBursts.AddDefaulted_GetRef();
			Burst.Location = Location;
			Burst.Color = Color;
			Burst.Effect = Effect;
			Burst.SizeScale = SizeScale;
			continue;
		}

		// 흩뿌린 발은 제 위치에서, 모양 패턴은 차례의 위치에서 올라간다.
		const FVector& ShellOrigin = ShellOrigins.IsValidIndex(Shell) ? ShellOrigins[Shell] : Origin;

		// 도화선으로 차례 터뜨리기: 다 같이 올라가되 뒤 발일수록 오래 올라 늦게 터진다.
		if (Volley.bStaggerByFuse && Volley.ShellDelaySeconds > 0.f && VolleyRiseSeconds > 0.f)
		{
			LaunchShell(ShellOrigin, Location, Color, Effect, SizeScale,
				VolleyRiseSeconds + Volley.ShellDelaySeconds * static_cast<float>(Shell));
			if (!bShowInProgress)
			{
				return;
			}
			continue;
		}

		if (Volley.ShellDelaySeconds > 0.f && Shell > 0)
		{
			// 줄지어 흐르는 느낌은 이 지연에서 나온다. 한꺼번에 쏘면 그냥 모양일 뿐이다.
			// 상승 시간이 같으므로 쏘는 간격이 그대로 터지는 간격이 된다.
			FPendingShell& Pending = PendingShells.AddDefaulted_GetRef();
			Pending.Location = Location;
			Pending.Origin = ShellOrigin;
			Pending.RiseSeconds = VolleyRiseSeconds;
			Pending.Color = Color;
			Pending.DelayRemaining = Volley.ShellDelaySeconds * static_cast<float>(Shell);
			Pending.Effect = Effect;
			Pending.SizeScale = SizeScale;
			continue;
		}

		LaunchShell(ShellOrigin, Location, Color, Effect, SizeScale, VolleyRiseSeconds);

		// OnBurst 를 받은 쪽이 연출을 멈췄다. 남은 발을 쏘지 않는다.
		if (!bShowInProgress)
		{
			return;
		}
	}

	if (bCarrier && CarrierBursts.Num() > 0)
	{
		LaunchCarrier(Origin, PatternCenter, MoveTemp(CarrierBursts), UniformColor, VolleyRiseSeconds);
		if (!bShowInProgress)
		{
			return;
		}
	}

	++FiredVolleys;
}

int32 UFF_FinaleShowSubsystem::GetRemainingShells() const
{
	// 오르는 발도 이미 쓴 발이다. 빼지 않으면 오르는 몇 초 동안 같은 발수를 또 쏜다.
	return FMath::Max(0, ActiveSpec.BurstCount - FiredBursts - PendingShells.Num() - InFlightBurstCount);
}

FString UFF_FinaleShowSubsystem::DescribeEffects(const FFF_FinaleGradeSpec& Spec)
{
	/* 칸 수와 채워진 수를 따로 보여준다.

	   `3종` 이라고만 찍으면 그중 None 이 섞여 있어도 모른다. 칸은 채웠다고 믿는데
	   가끔 안 터지는 원인이 대개 그 빈 칸이다.
	*/
	const int32 Slots = Spec.BurstEffects.Num();
	const int32 Assigned = Spec.CountAssignedEffects();

	if (Slots == 0)
	{
		return TEXT("없음");
	}
	if (Assigned == Slots)
	{
		return FString::Printf(TEXT("%d종"), Assigned);
	}
	return FString::Printf(TEXT("%d칸 중 %d종 (빈 칸 %d)"), Slots, Assigned, Slots - Assigned);
}

FFF_FinaleLaunchSpec UFF_FinaleShowSubsystem::ResolveLaunchSpec() const
{
	// 데이터 애셋이 있으면 그쪽 값. 없으면 구조체 기본값이다(시간·궤적만 있고 꼬리 모습은 없다).
	if (const UFF_FinaleShowData* Data = ResolveShowData())
	{
		return Data->Launch;
	}
	return FFF_FinaleLaunchSpec();
}

int32 UFF_FinaleShowSubsystem::CountActiveFlashLights() const
{
	// 번쩍임은 상승보다 훨씬 짧아서 오르는 발만 세면 된다.
	int32 Count = 0;
	for (const FInFlightShell& Shell : InFlightShells)
	{
		if (const AFF_FinaleShellTracer* Tracer = Shell.Tracer.Get())
		{
			Count += Tracer->IsFlashLightActive() ? 1 : 0;
		}
	}
	return Count;
}

void UFF_FinaleShowSubsystem::LaunchShell(
	const FVector& Origin, const FVector& Location, const FLinearColor& Color,
	UNiagaraSystem* Effect, const float SizeScale, const float RiseSeconds)
{
	TArray<FShellBurst> Bursts;
	FShellBurst& Burst = Bursts.AddDefaulted_GetRef();
	Burst.Location = Location;
	Burst.Color = Color;
	Burst.Effect = Effect;
	Burst.SizeScale = SizeScale;
	LaunchCarrier(Origin, Location, MoveTemp(Bursts), Color, RiseSeconds);
}

void UFF_FinaleShowSubsystem::LaunchCarrier(
	const FVector& Origin, const FVector& Target, TArray<FShellBurst>&& Bursts,
	const FLinearColor& Color, const float RiseSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !ActiveLaunch.IsEnabled())
	{
		// 쏘아 올리기를 껐다. 예전처럼 그 자리에서 터진다.
		for (const FShellBurst& Burst : Bursts)
		{
			SpawnShell(Burst.Location, Burst.Color, Burst.Effect.Get(), Burst.SizeScale);
			if (!bShowInProgress)
			{
				return;
			}
		}
		return;
	}

	FInFlightShell& Shell = InFlightShells.AddDefaulted_GetRef();
	Shell.Origin = Origin;
	Shell.Target = Target;
	Shell.RiseSeconds = RiseSeconds > 0.f ? RiseSeconds : ActiveLaunch.ResolveRiseSeconds(Target.Z - Origin.Z);
	Shell.WobblePhase = ShowRandom.FRandRange(0.f, 2.f * PI);
	InFlightBurstCount += Bursts.Num();
	Shell.Bursts = MoveTemp(Bursts);

	// 모습은 따로 만든다. 못 만들어도(테스트 월드 등) 시간과 터지는 자리는 그대로 돈다.
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;
	if (AFF_FinaleShellTracer* Tracer = World->SpawnActor<AFF_FinaleShellTracer>(
		AFF_FinaleShellTracer::StaticClass(), Origin, FRotator::ZeroRotator, SpawnParameters))
	{
		const bool bWithLight = CountActiveFlashLights() < FMath::Max(0, ActiveLaunch.MaxFlashLights);
		Tracer->Launch(ActiveLaunch, Color, bWithLight, Shell.WobblePhase);
		Shell.Tracer = Tracer;
	}

	++LaunchedShells;

	// 배열이 늘어나 Shell 참조가 깨질 수 있어 값으로 넘긴다. 받는 쪽이 연출을 멈춰도 여기서는 할 일이 끝났다.
	const float Rise = Shell.RiseSeconds;
	OnLaunch.Broadcast(Origin, Target, Rise);
}

bool UFF_FinaleShowSubsystem::TickInFlightShells(const float DeltaTime)
{
	/* 다 오른 발을 먼저 빼낸 뒤에 터뜨린다.

	   터뜨리면 OnBurst 가 울리는데, 받는 쪽이 연출을 멈추면 StopShow 가 배열을 비운다.
	   배열을 돌면서 터뜨리면 그 순간 인덱스가 배열 밖을 가리킨다(지연 발사와 같은 이유).
	*/
	const float TrailSpan = FMath::Max(0.f, ActiveLaunch.TrailSeconds) * FMath::Max(0.1f, ActiveSpec.TrailLength);

	TArray<FInFlightShell> Arrived;
	for (int32 Index = InFlightShells.Num() - 1; Index >= 0; --Index)
	{
		FInFlightShell& Shell = InFlightShells[Index];
		Shell.Elapsed += DeltaTime;

		if (Shell.Elapsed >= Shell.RiseSeconds)
		{
			InFlightBurstCount -= Shell.Bursts.Num();
			Arrived.Add(MoveTemp(Shell));
			InFlightShells.RemoveAtSwap(Index);
			continue;
		}

		if (AFF_FinaleShellTracer* Tracer = Shell.Tracer.Get())
		{
			const float Rise = FMath::Max(KINDA_SMALL_NUMBER, Shell.RiseSeconds);
			const float HeadAlpha = Shell.Elapsed / Rise;
			const float TailAlpha = FMath::Max(0.f, Shell.Elapsed - TrailSpan) / Rise;
			Tracer->UpdateFlight(
				ActiveLaunch.EvaluateRise(Shell.Origin, Shell.Target, HeadAlpha, Shell.WobblePhase),
				ActiveLaunch.EvaluateRise(Shell.Origin, Shell.Target, TailAlpha, Shell.WobblePhase));
		}
	}

	// 먼저 쏜 발이 먼저 터지게 오른 시간이 긴 순으로 터뜨린다. 소리 순서가 뒤집히지 않는다.
	Arrived.Sort([](const FInFlightShell& A, const FInFlightShell& B)
	{
		return (A.Elapsed - A.RiseSeconds) > (B.Elapsed - B.RiseSeconds);
	});

	for (const FInFlightShell& Shell : Arrived)
	{
		if (AFF_FinaleShellTracer* Tracer = Shell.Tracer.Get())
		{
			Tracer->Burst();
		}

		// 모양 알은 품은 불꽃을 한 프레임에 다 터뜨린다. 그래야 모양이 한꺼번에 핀다.
		for (const FShellBurst& Burst : Shell.Bursts)
		{
			if (!bShowInProgress)
			{
				break;
			}
			SpawnShell(Burst.Location, Burst.Color, Burst.Effect.Get(), Burst.SizeScale);
		}
	}

	return bShowInProgress;
}

void UFF_FinaleShowSubsystem::EnsureFallbackLayout()
{
	if (bFallbackLayoutApplied)
	{
		return;
	}

	UWorld* World = GetWorld();
	const UFF_FinaleShowData* Data = ResolveShowData();
	if (!World || !Data || Data->LayoutLevel.IsNull()
		|| (Data->FallbackLaunchSites.Num() == 0 && Data->FallbackViewpoints.Num() == 0))
	{
		return;
	}

	// 좌표는 그 레벨의 것이다. 다른 레벨에 세우면 엉뚱한 곳(땅속·바다 밖)에서 쏜다.
	// PIE 에서는 패키지 이름에 UEDPIE_0_ 접두어가 붙어 떼고 비교한다.
	const FString WorldPackage = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	if (WorldPackage != Data->LayoutLevel.ToSoftObjectPath().GetLongPackageName())
	{
		return;
	}
	bFallbackLayoutApplied = true;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;

	// 레벨에 놓은 발사 지점이 하나라도 있으면 그쪽을 쓴다. 섞으면 어느 쪽이 쏘는지 알 수 없다.
	TArray<AFF_FinaleLaunchSite*> PlacedSites;
	AFF_FinaleLaunchSite::CollectAll(World, PlacedSites);
	int32 SpawnedSites = 0;
	if (PlacedSites.Num() == 0)
	{
		for (int32 Index = 0; Index < Data->FallbackLaunchSites.Num(); ++Index)
		{
			const FFF_FinaleLayoutLaunchSite& Entry = Data->FallbackLaunchSites[Index];
			if (AFF_FinaleLaunchSite* Site = World->SpawnActor<AFF_FinaleLaunchSite>(
				AFF_FinaleLaunchSite::StaticClass(), Entry.Transform, SpawnParameters))
			{
				Site->BargeCount = Entry.BargeCount;
				Site->BargeSpacingCm = Entry.BargeSpacingCm;
				Site->Order = Index;
				++SpawnedSites;
			}
		}
	}

	bool bHasPlacedViewpoint = false;
	for (TActorIterator<AFF_FinaleViewpoint> It(World); It; ++It)
	{
		bHasPlacedViewpoint |= IsValid(*It);
	}

	int32 SpawnedViewpoints = 0;
	if (!bHasPlacedViewpoint)
	{
		for (const FFF_FinaleLayoutViewpoint& Entry : Data->FallbackViewpoints)
		{
			// BeginPlay 전에 값을 넣어야 틱(흘러가기) 설정이 그 값으로 잡힌다.
			AFF_FinaleViewpoint* Viewpoint = World->SpawnActorDeferred<AFF_FinaleViewpoint>(
				AFF_FinaleViewpoint::StaticClass(), Entry.Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Viewpoint)
			{
				continue;
			}

			Viewpoint->TourAct = Entry.TourAct;
			Viewpoint->TourOrder = Entry.TourOrder;
			Viewpoint->TourHoldSeconds = Entry.TourHoldSeconds;
			Viewpoint->TourBlendSeconds = Entry.TourBlendSeconds;
			Viewpoint->TourDriftOffset = Entry.TourDriftOffset;
			Viewpoint->Priority = Entry.Priority;
			if (UCameraComponent* Camera = Viewpoint->GetViewCamera())
			{
				Camera->SetFieldOfView(Entry.FieldOfView);
			}
			Viewpoint->FinishSpawning(Entry.Transform);
			++SpawnedViewpoints;
		}
	}

	UE_LOG(LogFestaFiestaFinale, Log,
		TEXT("[피날레] 레벨에 놓인 액터가 없어 기본 배치를 세웠다. 발사 지점 %d / 연출 시점 %d (데이터 %s)"),
		SpawnedSites, SpawnedViewpoints, *Data->GetName());
}

bool UFF_FinaleShowSubsystem::PrepareSiteTour()
{
	const UFF_FinaleShowData* Data = ResolveShowData();
	ActiveSiteTour = Data ? Data->SiteTour : FFF_FinaleSiteTourSpec();
	if (!ActiveSiteTour.bFollowPlacedFacilities)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	/* 입구: 손님이 생겨나는 게이트. 게이트에는 방향이 없어 축제장 가운데(SimOrigin)를 안쪽으로 본다.

	   게이트는 스폰 액터의 BeginPlay 에서 모이므로 연출 시점에는 이미 있다. 여럿이면 가중치가 큰 것.
	*/
	bool bHasEntrance = false;
	FVector Entrance = FVector::ZeroVector;
	FVector Inside = FVector::ZeroVector;
	if (const UFF_CrowdSimulationSubsystem* Crowd = World->GetSubsystem<UFF_CrowdSimulationSubsystem>())
	{
		float BestWeight = -1.f;
		for (const FFF_CrowdGate& Gate : Crowd->GetValidGates())
		{
			if (Gate.bAllowSpawn && Gate.GateWeight > BestWeight)
			{
				BestWeight = Gate.GateWeight;
				Entrance = Gate.GateLocation;
				bHasEntrance = true;
			}
		}
		Inside = Crowd->SimOrigin;
	}

	// 시설: 길·바닥 타일·조명은 건물이 아니라 뺀다. 철거된 것도 뺀다.
	TArray<FVector> Sites;
	if (const UFFZSWorldSubsystem* Zone = World->GetSubsystem<UFFZSWorldSubsystem>())
	{
		const FFFZSReadModel Model = Zone->BuildReadModel();
		for (const FFFZSFacilityReadModel& Facility : Model.Facilities)
		{
			if (Facility.State == EFFZSFacilityState::Removed
				|| Facility.Category == EFFZSFacilityCategory::Road
				|| Facility.Category == EFFZSFacilityCategory::Tile
				|| Facility.Category == EFFZSFacilityCategory::Lighting)
			{
				continue;
			}
			Sites.Add(Facility.WorldLocation);
		}
	}

	if (!bHasEntrance && Sites.Num() == 0)
	{
		return false;
	}

	// 입구를 못 찾았으면 첫 시설에서 시작한다. 그래도 지은 것은 다 지난다.
	if (!bHasEntrance)
	{
		Entrance = Sites[0];
	}

	SiteTourRoute = FFF_FinaleSiteTourRoute::Build(
		Entrance, Sites, ActiveSiteTour.ClusterCm, ActiveSiteTour.MaxStops);

	// 시설이 하나도 없으면 입구에서 축제장 가운데로 들어가는 길만 보여 준다.
	if (SiteTourRoute.Stops.Num() < 2 && !Inside.IsNearlyZero())
	{
		SiteTourRoute = FFF_FinaleSiteTourRoute::Build(Entrance, { Inside }, ActiveSiteTour.ClusterCm, 1);
	}

	const float MinSeconds = FMath::Max(1.f, FMath::Min(ActiveSiteTour.MinSeconds, ActiveSiteTour.MaxSeconds));
	const float MaxSeconds = FMath::Max(MinSeconds, ActiveSiteTour.MaxSeconds);
	SiteTourRoute.BuildTimeline(
		FMath::Max(100.f, ActiveSiteTour.SpeedCmPerSecond), ActiveSiteTour.StopSeconds, MinSeconds, MaxSeconds);
	SiteTourSeconds = SiteTourRoute.GetDuration();

	// 빠르게 날수록 높이 난다(HeightPerSpeedSeconds 주석). 뒤로 떨어진 거리도 같은 비율로 늘려 각도를 지킨다.
	const float AverageSpeed = SiteTourSeconds > 0.f ? SiteTourRoute.GetLength() / SiteTourSeconds : 0.f;
	const float BaseHeight = FMath::Max(200.f, ActiveSiteTour.HeightCm);
	SiteTourHeight = FMath::Max(BaseHeight, AverageSpeed * ActiveSiteTour.HeightPerSpeedSeconds);
	SiteTourTrail = FMath::Max(100.f, ActiveSiteTour.TrailCm) * (SiteTourHeight / BaseHeight);

	UE_LOG(LogFestaFiestaFinale, Log,
		TEXT("[피날레] 순회 경로: 입구 %s → 시설 묶음 %d곳(첫 곳까지 %.0fm) · 높이 %.0fm · 뒤로 %.0fm · 날아 들어옴 %.1f초 + 순회 %.1f초"),
		*SiteTourRoute.Stops[0].ToCompactString(), SiteTourRoute.Stops.Num() - 1,
		SiteTourRoute.GetStopDistance(1) / 100.f, SiteTourHeight / 100.f, SiteTourTrail / 100.f,
		ActiveSiteTour.FlyInSeconds, SiteTourSeconds);

	if (!SiteTourCamera)
	{
		SiteTourCamera = World->SpawnActorDeferred<AFF_FinaleViewpoint>(
			AFF_FinaleViewpoint::StaticClass(), FTransform(Entrance), nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!SiteTourCamera)
		{
			return false;
		}
		// 레벨의 고정 순회·고정 연출 자리 고르기에 섞이지 않게 한다.
		SiteTourCamera->bIncludeInTour = false;
		SiteTourCamera->TourAct = EFF_FinaleTourAct::SiteTour;
		SiteTourCamera->TourDriftOffset = FVector::ZeroVector;
		SiteTourCamera->FinishSpawning(FTransform(Entrance));
	}

	// 날아 들어오는 동안은 출발점에서 기다리므로 머무는 시간에 그만큼 더한다(UpdateSiteTourCamera).
	SiteTourCamera->TourHoldSeconds = FMath::Max(0.f, ActiveSiteTour.FlyInSeconds) + SiteTourSeconds;
	SiteTourCamera->TourBlendSeconds = FMath::Max(0.f, ActiveSiteTour.FlyInSeconds);
	if (UCameraComponent* Camera = SiteTourCamera->GetViewCamera())
	{
		Camera->SetFieldOfView(ActiveSiteTour.FieldOfView);
	}

	// 경로 시작에 미리 놓는다. 해변에서 날아 들어오는 블렌드가 이 자리를 향한다.
	UpdateSiteTourCamera(0.f, true);
	return true;
}

void UFF_FinaleShowSubsystem::UpdateSiteTourCamera(const float HeldSeconds, const bool bSnap)
{
	if (!SiteTourCamera || !SiteTourRoute.IsValid())
	{
		return;
	}

	/* 날아 들어오는 동안(FlyInSeconds)은 출발점에 서서 입구를 본다. 블렌드가 끝난 뒤에 출발한다.

	   예전에는 블렌드 중에 이미 경로를 따라 움직여서, 화면이 순회 카메라에 닿을 때쯤엔 입구 옆 시설을
	   지나친 뒤였다(2026-10-05 피드백 "입구쪽 건물이 짤림").
	*/
	const float RouteSeconds = HeldSeconds - FMath::Max(0.f, ActiveSiteTour.FlyInSeconds);
	const float FocusDistance = SiteTourRoute.DistanceAt(RouteSeconds);

	FVector Eye;
	FVector Focus;
	SiteTourRoute.EvaluateView(FocusDistance, SiteTourTrail, SiteTourHeight, Eye, Focus);

	// 물러선 방향은 천천히 따라간다. 경로가 꺾이는 곳에서 카메라가 한 번에 휙 돌아가지 않는다.
	const FVector WantedDirection = (Focus - Eye).GetSafeNormal2D();
	const UWorld* World = GetWorld();
	const float DeltaSeconds = World ? World->GetDeltaSeconds() : 0.f;
	if (bSnap || SiteTourBoomDirection.IsNearlyZero())
	{
		SiteTourBoomDirection = WantedDirection.IsNearlyZero() ? FVector::ForwardVector : WantedDirection;
	}
	else if (!WantedDirection.IsNearlyZero())
	{
		constexpr float BoomTurnDegreesPerSecond = 120.f;
		SiteTourBoomDirection = FMath::VInterpNormalRotationTo(
			SiteTourBoomDirection, WantedDirection, DeltaSeconds, BoomTurnDegreesPerSecond).GetSafeNormal2D();
	}

	Eye = Focus - SiteTourBoomDirection * SiteTourTrail + FVector(0.f, 0.f, SiteTourHeight);

	// 땅이 아니라 시설 몸통쯤을 겨눈다. 바닥을 겨누면 건물 윗부분이 화면 위로 잘린다.
	constexpr float AimAboveGroundCm = 400.f;
	const FRotator Rotation = (Focus + FVector(0.f, 0.f, AimAboveGroundCm) - Eye).Rotation();
	SiteTourCamera->SetActorLocationAndRotation(Eye, Rotation);
}
