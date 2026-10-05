// Copyright Epic Games, Inc. All Rights Reserved.

// 역할: 축제장 순회 경로 — 입구에서 시작해 모든 시설 묶음을 한 번씩 지나고, 곡선이 경유지를 지나는지 본다.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Finale/FF_FinaleSiteTour.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFF_FinaleSiteTourRouteVisitsAll, "FestaFiesta.Finale.SiteTour.RouteStartsAtEntranceAndVisitsAll", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FFF_FinaleSiteTourRouteVisitsAll::RunTest(const FString& Parameters)
{
	const FVector Entrance(0.f, 0.f, 500.f);
	const float Cell = 3000.f;

	// 칸 하나에 하나씩 떨어진 시설 다섯 개와, 그중 한 칸에 붙어 있는 시설 하나.
	TArray<FVector> Sites = {
		FVector(20000.f, 0.f, 500.f), FVector(5000.f, 500.f, 500.f), FVector(40000.f, 10000.f, 500.f),
		FVector(10000.f, 20000.f, 500.f), FVector(30000.f, 30000.f, 500.f), FVector(5500.f, 900.f, 500.f)
	};

	const FFF_FinaleSiteTourRoute Route = FFF_FinaleSiteTourRoute::Build(Entrance, Sites, Cell, 10);
	TestTrue(TEXT("경로가 있다"), Route.IsValid());
	TestTrue(TEXT("입구에서 시작한다"), Route.Stops[0].Equals(Entrance));
	TestTrue(TEXT("곡선의 시작이 입구다"), Route.Evaluate(0.f).Equals(Entrance, 1.f));

	// 붙은 두 시설은 한 칸이라 경유지가 하나 줄어든다.
	TestEqual(TEXT("칸마다 한 번씩"), Route.Stops.Num(), 1 + 5);

	// 모든 시설이 어느 경유지 한 칸 안에 든다.
	for (const FVector& Site : Sites)
	{
		bool bCovered = false;
		for (const FVector& Stop : Route.Stops)
		{
			bCovered |= FVector::Dist2D(Site, Stop) <= Cell * 1.5f;
		}
		TestTrue(*FString::Printf(TEXT("시설 %s 를 지난다"), *Site.ToCompactString()), bCovered);
	}

	// 첫 경유지는 입구에서 가장 가까운 묶음이다.
	float Closest = TNumericLimits<float>::Max();
	for (const FVector& Site : Sites)
	{
		Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist2D(Entrance, Site)));
	}
	TestTrue(TEXT("가까운 곳부터 간다"), FVector::Dist2D(Entrance, Route.Stops[1]) <= Closest + Cell);

	// 곡선은 경유지를 지나고, 끝은 마지막 경유지다.
	TestTrue(TEXT("끝은 마지막 경유지"), Route.Evaluate(Route.GetLength()).Equals(Route.Stops.Last(), 1.f));
	TestTrue(TEXT("길이는 경유지를 직선으로 이은 것보다 짧지 않다"), [&Route]()
	{
		float Straight = 0.f;
		for (int32 Index = 1; Index < Route.Stops.Num(); ++Index)
		{
			Straight += FVector::Dist(Route.Stops[Index - 1], Route.Stops[Index]);
		}
		return Route.GetLength() + 1.f >= Straight;
	}());

	// 거리가 늘면 위치가 앞으로 간다(되감기지 않는다) — 거리 순으로 샘플 사이 거리가 거리 차이를 넘지 않는다.
	FVector Previous = Route.Evaluate(0.f);
	const float Step = Route.GetLength() / 50.f;
	for (int32 Index = 1; Index <= 50; ++Index)
	{
		const FVector Point = Route.Evaluate(Step * Index);
		TestTrue(*FString::Printf(TEXT("%d번째 구간은 걸음보다 멀리 튀지 않는다"), Index),
			FVector::Dist(Previous, Point) <= Step + 1.f);
		Previous = Point;
	}
	return true;
}

// 시설이 아주 많아도 경유지 수 상한을 지킨다. 시설이 없으면 입구 하나뿐이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFF_FinaleSiteTourRouteLimits, "FestaFiesta.Finale.SiteTour.RouteRespectsLimits", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FFF_FinaleSiteTourRouteLimits::RunTest(const FString& Parameters)
{
	TArray<FVector> Many;
	for (int32 X = 0; X < 20; ++X)
	{
		for (int32 Y = 0; Y < 20; ++Y)
		{
			Many.Add(FVector(X * 4000.f, Y * 4000.f, 300.f));
		}
	}

	for (const int32 Limit : { 1, 4, 10 })
	{
		const FFF_FinaleSiteTourRoute Route = FFF_FinaleSiteTourRoute::Build(FVector::ZeroVector, Many, 3000.f, Limit);
		TestTrue(*FString::Printf(TEXT("상한 %d 를 지킨다"), Limit), Route.Stops.Num() - 1 <= Limit);
		TestTrue(*FString::Printf(TEXT("상한 %d 에서도 어딘가는 지난다"), Limit), Route.Stops.Num() >= 2);
	}

	const FFF_FinaleSiteTourRoute Empty = FFF_FinaleSiteTourRoute::Build(FVector(1.f, 2.f, 3.f), {}, 3000.f, 10);
	TestEqual(TEXT("시설이 없으면 입구뿐"), Empty.Stops.Num(), 1);
	TestEqual(TEXT("길이 0"), Empty.GetLength(), 0.f);
	TestTrue(TEXT("어디를 물어도 입구"), Empty.Evaluate(500.f).Equals(FVector(1.f, 2.f, 3.f)));
	return true;
}

// 다음에 겨눌 경유지는 지나온 순서대로 바뀌고, 입구를 다시 겨누지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFF_FinaleSiteTourNextStop, "FestaFiesta.Finale.SiteTour.NextStopFollowsRoute", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FFF_FinaleSiteTourNextStop::RunTest(const FString& Parameters)
{
	const TArray<FVector> Sites = { FVector(10000.f, 0.f, 0.f), FVector(20000.f, 5000.f, 0.f), FVector(30000.f, 0.f, 0.f) };
	const FFF_FinaleSiteTourRoute Route = FFF_FinaleSiteTourRoute::Build(FVector::ZeroVector, Sites, 1000.f, 10);

	int32 PreviousIndex = 1;
	for (int32 Step = 0; Step <= 40; ++Step)
	{
		const FVector Next = Route.NextStopAfter(Route.GetLength() * Step / 40.f);
		const int32 Index = Route.Stops.IndexOfByPredicate([&Next](const FVector& Stop) { return Stop.Equals(Next); });
		TestTrue(*FString::Printf(TEXT("%d단계: 입구가 아닌 경유지"), Step), Index >= 1);
		TestTrue(*FString::Printf(TEXT("%d단계: 뒤로 돌아가지 않는다"), Step), Index >= PreviousIndex);
		PreviousIndex = Index;
	}
	TestTrue(TEXT("끝에서는 마지막 경유지"), Route.NextStopAfter(Route.GetLength() + 1.f).Equals(Route.Stops.Last()));

	const FFF_FinaleSiteTourRoute EntranceOnly = FFF_FinaleSiteTourRoute::Build(FVector(5.f, 6.f, 7.f), {}, 1000.f, 10);
	TestTrue(TEXT("입구뿐이면 입구"), EntranceOnly.NextStopAfter(0.f).Equals(FVector(5.f, 6.f, 7.f)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
