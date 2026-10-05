// Copyright Epic Games, Inc. All Rights Reserved.

// 역할: 바지선에서 쏘아 올리는 피날레 — 상승 시간·궤적의 성질과, 쏘아 올린 발이 빠짐없이 한 번씩 터지는지 본다.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Finale/FF_FinaleShowTypes.h"

// 높이가 높을수록 오래 오르고, 범위 안에서는 높이의 제곱근을 따른다. 범위 밖은 묶인다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFF_FinaleLaunchRiseTime, "FestaFiesta.Finale.Launch.RiseTimeFollowsHeight", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FFF_FinaleLaunchRiseTime::RunTest(const FString& Parameters)
{
	const FFF_FinaleLaunchSpec Spec;

	float Previous = 0.f;
	for (float Height = 2000.f; Height <= 60000.f; Height += 2000.f)
	{
		const float Seconds = Spec.ResolveRiseSeconds(Height);
		TestTrue(*FString::Printf(TEXT("%.0fcm 는 더 낮은 발보다 빨리 끝나지 않는다"), Height), Seconds >= Previous);
		TestTrue(*FString::Printf(TEXT("%.0fcm 는 범위 안"), Height),
			Seconds >= Spec.MinRiseSeconds - KINDA_SMALL_NUMBER && Seconds <= Spec.MaxRiseSeconds + KINDA_SMALL_NUMBER);
		Previous = Seconds;
	}

	// 범위에 안 걸리는 두 높이를 고른다. 높이가 4배면 시간은 2배다(제곱근).
	const float Low = Spec.ReferenceHeightCm * 0.5f;
	const float LowSeconds = Spec.ResolveRiseSeconds(Low);
	const float HighSeconds = Spec.ResolveRiseSeconds(Low * 4.f);
	const bool bBothInsideRange = LowSeconds > Spec.MinRiseSeconds && HighSeconds < Spec.MaxRiseSeconds;
	TestTrue(TEXT("고른 두 높이가 범위 안이다"), bBothInsideRange);
	if (bBothInsideRange)
	{
		TestNearlyEqual(TEXT("높이 4배면 시간 2배"), HighSeconds / LowSeconds, 2.f, 0.01f);
	}

	TestNearlyEqual(TEXT("기준 높이에서는 기준 시간"),
		Spec.ResolveRiseSeconds(Spec.ReferenceHeightCm), Spec.ReferenceRiseSeconds, 0.001f);
	TestTrue(TEXT("땅바닥은 최소 시간"), Spec.ResolveRiseSeconds(0.f) >= Spec.MinRiseSeconds - KINDA_SMALL_NUMBER);
	return true;
}

// 궤적은 발사 자리에서 시작해 터질 자리에서 끝나고, 오를수록 느려지며, 끝에서 눕는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFF_FinaleLaunchTrajectory, "FestaFiesta.Finale.Launch.TrajectoryDeceleratesAndLands", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FFF_FinaleLaunchTrajectory::RunTest(const FString& Parameters)
{
	FFF_FinaleLaunchSpec Spec;
	const FVector Start(1000.f, -2000.f, 70.f);
	const FVector Target(9000.f, 4000.f, 25000.f);

	for (const float Wobble : { 0.f, 300.f })
	{
		Spec.WobbleCm = Wobble;
		for (const float Phase : { 0.f, 1.3f, 4.f })
		{
			const FString Case = FString::Printf(TEXT("흔들림 %.0f 위상 %.1f"), Wobble, Phase);
			TestTrue(Case + TEXT(": 발사 자리에서 시작"), Spec.EvaluateRise(Start, Target, 0.f, Phase).Equals(Start, 0.01f));
			TestTrue(Case + TEXT(": 터질 자리에서 끝"), Spec.EvaluateRise(Start, Target, 1.f, Phase).Equals(Target, 0.01f));
		}
	}

	// 높이는 계속 오르고, 오르는 속도는 계속 준다(감속).
	Spec.WobbleCm = 0.f;
	constexpr int32 Steps = 50;
	float PreviousZ = Start.Z;
	float PreviousRise = TNumericLimits<float>::Max();
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const float Alpha = static_cast<float>(Step) / Steps;
		const float Z = Spec.EvaluateRise(Start, Target, Alpha).Z;
		const float Rise = Z - PreviousZ;
		TestTrue(*FString::Printf(TEXT("%d단계에서 내려가지 않는다"), Step), Rise >= 0.f);
		TestTrue(*FString::Printf(TEXT("%d단계에서 빨라지지 않는다"), Step), Rise <= PreviousRise + 0.01f);
		PreviousZ = Z;
		PreviousRise = Rise;
	}

	// 수직이 수평보다 먼저 끝난다 — 비스듬히 쏜 발이 끝에서 옆으로 눕는다.
	const FVector Delta = Target - Start;
	for (const float Alpha : { 0.2f, 0.5f, 0.8f, 0.95f })
	{
		const FVector At = Spec.EvaluateRise(Start, Target, Alpha) - Start;
		const float VerticalDone = At.Z / Delta.Z;
		const float HorizontalDone = FVector2D(At).Size() / FVector2D(Delta).Size();
		TestTrue(*FString::Printf(TEXT("%.2f 지점에서 수직이 더 많이 왔다"), Alpha), VerticalDone >= HorizontalDone);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Finale/FF_FinaleShowSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	// 데이터 애셋 값에 기대지 않게 시험용 등급을 직접 만든다.
	UFF_FinaleShowData* MakeTestShowData(const bool bLaunchFromSite)
	{
		UFF_FinaleShowData* Data = NewObject<UFF_FinaleShowData>(GetTransientPackage());
		FFF_FinaleGradeSpec& Spec = Data->GradeC;
		Spec = FFF_FinaleDefaults::MakeSpec(EFF_FinaleGrade::C);
		Spec.BurstCount = 24;
		Spec.DurationSeconds = 6.f;
		Spec.SimultaneousShells = 3;
		Spec.BurstHeightMin = 12000.f;
		Spec.BurstHeightMax = 30000.f;
		Data->Launch.bLaunchFromSite = bLaunchFromSite;
		return Data;
	}

	struct FShowRun
	{
		int32 MaxLaunched = 0;
		int32 MaxSum = 0;
		int32 FirstBurstTick = INDEX_NONE;
		int32 Ticks = 0;
		bool bEnded = false;
	};

	FShowRun RunShow(UFF_FinaleShowSubsystem& Finale, const float Step, const int32 MaxTicks)
	{
		FShowRun Run;
		for (; Run.Ticks < MaxTicks && Finale.IsShowInProgress(); ++Run.Ticks)
		{
			Finale.Tick(Step);
			Run.MaxLaunched = FMath::Max(Run.MaxLaunched, Finale.GetLaunchedShellCount());
			Run.MaxSum = FMath::Max(Run.MaxSum, Finale.GetFiredBurstCount() + Finale.GetShellsInFlight());
			if (Run.FirstBurstTick == INDEX_NONE && Finale.GetFiredBurstCount() > 0)
			{
				Run.FirstBurstTick = Run.Ticks;
			}
		}
		Run.bEnded = !Finale.IsShowInProgress();
		return Run;
	}
}

// 쏘아 올린 발은 오르는 시간이 지나야 터지고, 정한 발수만큼 빠짐없이 한 번씩 터진 뒤 연출이 끝난다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFF_FinaleLaunchEveryShellBurstsOnce,
	"FestaFiesta.Finale.Launch.EveryShellBurstsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFF_FinaleLaunchEveryShellBurstsOnce::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game))
	{
		Fixture.ForwardErrorMessages(this);
		return false;
	}

	UFF_FinaleShowSubsystem* Finale = Fixture.GetTestWorld()->GetSubsystem<UFF_FinaleShowSubsystem>();
	if (!TestNotNull(TEXT("피날레 서브시스템"), Finale))
	{
		return false;
	}

	// 소프트 참조만 들고 있으면 시험 도중 정리될 수 있어 붙잡아 둔다.
	const TStrongObjectPtr<UFF_FinaleShowData> Data(MakeTestShowData(true));
	Finale->ShowData = Data.Get();
	Finale->SetCameraTakeoverEnabled(false);

	const FFF_FinaleGradeSpec& Spec = Data->GradeC;
	const float Step = 0.05f;

	// 끝나는 데 필요한 시간의 상한: 발사 상한(지속 2배) + 가장 긴 상승 + 여운.
	const float Budget = Spec.DurationSeconds * 2.f + Data->Launch.MaxRiseSeconds + Finale->TailSeconds + 2.f;
	const int32 MaxTicks = FMath::CeilToInt(Budget / Step);

	if (!TestTrue(TEXT("연출이 시작된다"), Finale->StartShowWithGrade(EFF_FinaleGrade::C)))
	{
		return false;
	}

	const FShowRun Run = RunShow(*Finale, Step, MaxTicks);

	TestTrue(TEXT("상한 시간 안에 끝난다"), Run.bEnded);
	TestEqual(TEXT("정한 발수만큼 터진다"), Finale->GetFiredBurstCount(), Spec.BurstCount);
	TestEqual(TEXT("정한 발수만큼 쏘아 올린다"), Finale->GetLaunchedShellCount(), Spec.BurstCount);
	TestTrue(TEXT("쏘아 올린 발수가 정한 발수를 넘은 적이 없다"), Run.MaxLaunched <= Spec.BurstCount);
	TestTrue(TEXT("터진 발 + 오르는 발이 정한 발수를 넘은 적이 없다"), Run.MaxSum <= Spec.BurstCount);
	TestEqual(TEXT("끝나면 오르는 발이 없다"), Finale->GetShellsInFlight(), 0);

	// 가장 짧은 상승 시간 전에는 아무것도 터지지 않는다.
	const float MinRise = Data->Launch.ResolveRiseSeconds(Spec.BurstHeightMin);
	TestTrue(TEXT("첫 발은 오르는 시간이 지난 뒤에 터진다"),
		Run.FirstBurstTick != INDEX_NONE && (Run.FirstBurstTick + 1) * Step >= MinRise - Step);
	return true;
}

// 쏘아 올리기를 끄면 예전처럼 그 자리에서 바로 터진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFF_FinaleLaunchDisabledBurstsInPlace,
	"FestaFiesta.Finale.Launch.DisabledBurstsInPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFF_FinaleLaunchDisabledBurstsInPlace::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game))
	{
		Fixture.ForwardErrorMessages(this);
		return false;
	}

	UFF_FinaleShowSubsystem* Finale = Fixture.GetTestWorld()->GetSubsystem<UFF_FinaleShowSubsystem>();
	if (!TestNotNull(TEXT("피날레 서브시스템"), Finale))
	{
		return false;
	}

	const TStrongObjectPtr<UFF_FinaleShowData> Data(MakeTestShowData(false));
	Finale->ShowData = Data.Get();
	Finale->SetCameraTakeoverEnabled(false);

	if (!TestTrue(TEXT("연출이 시작된다"), Finale->StartShowWithGrade(EFF_FinaleGrade::C)))
	{
		return false;
	}

	Finale->Tick(0.05f);
	TestTrue(TEXT("첫 틱에 바로 터진다"), Finale->GetFiredBurstCount() > 0);
	TestEqual(TEXT("오르는 발이 없다"), Finale->GetShellsInFlight(), 0);
	TestEqual(TEXT("쏘아 올린 발로 세지 않는다"), Finale->GetLaunchedShellCount(), 0);

	const FShowRun Run = RunShow(*Finale, 0.05f, 1000);
	TestTrue(TEXT("끝난다"), Run.bEnded);
	TestEqual(TEXT("정한 발수만큼 터진다"), Finale->GetFiredBurstCount(), Data->GradeC.BurstCount);
	return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

// 한 순간에 피는 모양(원)은 알 하나가 올라가 한꺼번에 피고, 줄지어 나가는 일렬은 발마다 올라간다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFF_FinaleLaunchPatternRidesOneShell,
	"FestaFiesta.Finale.Launch.PatternRidesOneShell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFF_FinaleLaunchPatternRidesOneShell::RunTest(const FString& Parameters)
{
	for (const bool bSequential : { false, true })
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game))
		{
			Fixture.ForwardErrorMessages(this);
			return false;
		}

		UFF_FinaleShowSubsystem* Finale = Fixture.GetTestWorld()->GetSubsystem<UFF_FinaleShowSubsystem>();
		if (!TestNotNull(TEXT("피날레 서브시스템"), Finale))
		{
			return false;
		}

		const TStrongObjectPtr<UFF_FinaleShowData> Data(MakeTestShowData(true));
		FFF_FinaleVolley Volley;
		Volley.Pattern = bSequential ? EFF_FinaleVolleyPattern::Line : EFF_FinaleVolleyPattern::Ring;
		Volley.ShellCount = 8;
		Volley.ShellDelaySeconds = bSequential ? 0.1f : 0.f;
		Volley.ExtentCm = 6000.f;
		Data->GradeC.Volleys = { Volley };
		Data->GradeC.SimultaneousShells = 1;
		Data->GradeC.BurstCount = 24;
		Finale->ShowData = Data.Get();
		Finale->SetCameraTakeoverEnabled(false);

		const FString Case = bSequential ? TEXT("일렬") : TEXT("원");
		if (!TestTrue(Case + TEXT(": 연출이 시작된다"), Finale->StartShowWithGrade(EFF_FinaleGrade::C)))
		{
			return false;
		}

		// 첫 발이 터지는 순간 몇 발이 같이 피는지 본다.
		int32 FirstBloom = 0;
		for (int32 Tick = 0; Tick < 2000 && Finale->IsShowInProgress(); ++Tick)
		{
			Finale->Tick(0.02f);
			if (FirstBloom == 0)
			{
				FirstBloom = Finale->GetFiredBurstCount();
			}
		}

		TestFalse(Case + TEXT(": 끝난다"), Finale->IsShowInProgress());
		TestEqual(Case + TEXT(": 정한 발수만큼 터진다"), Finale->GetFiredBurstCount(), Data->GradeC.BurstCount);
		if (bSequential)
		{
			TestEqual(Case + TEXT(": 발마다 쏘아 올린다"), Finale->GetLaunchedShellCount(), Data->GradeC.BurstCount);
			TestTrue(Case + TEXT(": 처음엔 한 발씩 핀다"), FirstBloom < Volley.ShellCount);
		}
		else
		{
			TestEqual(Case + TEXT(": 모양마다 알 하나"),
				Finale->GetLaunchedShellCount(), Data->GradeC.BurstCount / Volley.ShellCount);
			TestEqual(Case + TEXT(": 모양이 한꺼번에 핀다"), FirstBloom, Volley.ShellCount);
		}
	}
	return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

// 큰 모양(도화선 차례)은 다 같이 올라가 하나씩 터지고, 진행률이 차기 전에는 나오지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFF_FinaleLaunchBigShapeLaterAndStaggered,
	"FestaFiesta.Finale.Launch.BigShapeComesLaterAndBurstsInTurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFF_FinaleLaunchBigShapeLaterAndStaggered::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game))
	{
		Fixture.ForwardErrorMessages(this);
		return false;
	}

	UFF_FinaleShowSubsystem* Finale = Fixture.GetTestWorld()->GetSubsystem<UFF_FinaleShowSubsystem>();
	if (!TestNotNull(TEXT("피날레 서브시스템"), Finale))
	{
		return false;
	}

	const TStrongObjectPtr<UFF_FinaleShowData> Data(MakeTestShowData(true));
	FFF_FinaleVolley Basic;
	Basic.Pattern = EFF_FinaleVolleyPattern::Single;
	FFF_FinaleVolley Big;
	Big.Pattern = EFF_FinaleVolleyPattern::Line;
	Big.ShellCount = 5;
	Big.ShellDelaySeconds = 0.2f;
	Big.ExtentCm = 15000.f;
	Big.bStaggerByFuse = true;
	Big.ShowProgressFrom = 0.6f;
	Big.Weight = 1000.f;
	Data->GradeC.Volleys = { Basic, Big };
	Data->GradeC.SimultaneousShells = 1;
	Data->GradeC.BurstCount = 20;
	Finale->ShowData = Data.Get();
	Finale->SetCameraTakeoverEnabled(false);

	if (!TestTrue(TEXT("연출이 시작된다"), Finale->StartShowWithGrade(EFF_FinaleGrade::C)))
	{
		return false;
	}

	const float Step = 0.02f;
	const int32 Total = Data->GradeC.BurstCount;
	int32 PreviousLaunched = 0;
	int32 PreviousFired = 0;
	bool bSawBigShape = false;
	bool bBigTooEarly = false;
	int32 MaxBloomAfterBig = 0;
	for (int32 Tick = 0; Tick < 3000 && Finale->IsShowInProgress(); ++Tick)
	{
		Finale->Tick(Step);
		const int32 Launched = Finale->GetLaunchedShellCount();
		if (Launched - PreviousLaunched >= Big.ShellCount)
		{
			// 한 틱에 다섯 발이 함께 올라갔다 = 큰 모양. 그 전까지 쓴 발이 60% 를 넘었어야 한다.
			bSawBigShape = true;
			bBigTooEarly |= static_cast<float>(PreviousLaunched) / Total < Big.ShowProgressFrom - 0.001f;
		}
		const int32 Fired = Finale->GetFiredBurstCount();
		if (bSawBigShape)
		{
			MaxBloomAfterBig = FMath::Max(MaxBloomAfterBig, Fired - PreviousFired);
		}
		PreviousLaunched = Launched;
		PreviousFired = Fired;
	}

	TestFalse(TEXT("끝난다"), Finale->IsShowInProgress());
	TestTrue(TEXT("큰 모양이 나왔다"), bSawBigShape);
	TestFalse(TEXT("진행률이 차기 전에는 큰 모양이 없다"), bBigTooEarly);
	TestTrue(TEXT("큰 모양은 한꺼번에 피지 않고 차례로 터진다"), MaxBloomAfterBig < Big.ShellCount);
	TestEqual(TEXT("정한 발수만큼 터진다"), Finale->GetFiredBurstCount(), Total);
	return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
