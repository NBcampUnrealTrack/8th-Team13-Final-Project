// FF_FinaleSiteTour.cpp
// 역할: 피날레 2막(축제장 순회)에서 카메라가 날아갈 경로를 만든다. 입구에서 시작해 설치된 시설을 훑는다.

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Finale/FF_FinaleSiteTour.h"

namespace
{
	// 경유지 사이를 몇 토막으로 나눠 곡선을 그릴지. 길이 계산과 위치 보간이 이 점들을 쓴다.
	constexpr int32 SamplesPerSegment = 24;

	// 칸을 키워 다시 묶을 때 곱하는 값.
	constexpr float ClusterGrowth = 1.5f;

	// 칸 크기로 시설을 묶어 칸마다 평균 자리 하나를 낸다. 칸 순서는 처음 나온 순서다.
	void ClusterSites(const TArray<FVector>& Sites, const float CellCm, TArray<FVector>& OutCenters)
	{
		OutCenters.Reset();
		TMap<FIntPoint, int32> CellToIndex;
		TArray<int32> Counts;

		const float Cell = FMath::Max(1.f, CellCm);
		for (const FVector& Site : Sites)
		{
			const FIntPoint Key(FMath::FloorToInt(Site.X / Cell), FMath::FloorToInt(Site.Y / Cell));
			if (const int32* Found = CellToIndex.Find(Key))
			{
				OutCenters[*Found] += Site;
				++Counts[*Found];
				continue;
			}
			CellToIndex.Add(Key, OutCenters.Num());
			OutCenters.Add(Site);
			Counts.Add(1);
		}

		for (int32 Index = 0; Index < OutCenters.Num(); ++Index)
		{
			OutCenters[Index] /= static_cast<float>(Counts[Index]);
		}
	}

	// 균일 캣멀롬. 끝 구간은 끝점을 한 번 더 써서 곡선이 경유지를 벗어나지 않게 한다.
	FVector CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, const float T)
	{
		const float T2 = T * T;
		const float T3 = T2 * T;
		return 0.5f * ((2.f * P1)
			+ (-P0 + P2) * T
			+ (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2
			+ (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3);
	}
}

FFF_FinaleSiteTourRoute FFF_FinaleSiteTourRoute::Build(
	const FVector& Entrance, const TArray<FVector>& Sites, const float ClusterCm, const int32 MaxStops)
{
	FFF_FinaleSiteTourRoute Route;
	Route.Stops.Add(Entrance);

	// 경유지가 상한을 넘으면 칸을 키워 다시 묶는다. 시설이 아주 많아도 몇 번이면 줄어든다.
	TArray<FVector> Centers;
	float Cell = FMath::Max(100.f, ClusterCm);
	const int32 Limit = FMath::Max(1, MaxStops);
	ClusterSites(Sites, Cell, Centers);
	for (int32 Guard = 0; Centers.Num() > Limit && Guard < 16; ++Guard)
	{
		Cell *= ClusterGrowth;
		ClusterSites(Sites, Cell, Centers);
	}

	// 입구에서 가장 가까운 것부터 차례로 잇는다. 같은 거리면 먼저 나온 것.
	FVector Cursor = Entrance;
	while (Centers.Num() > 0)
	{
		int32 Nearest = 0;
		double NearestDistance = FVector::DistSquared2D(Cursor, Centers[0]);
		for (int32 Index = 1; Index < Centers.Num(); ++Index)
		{
			const double Distance = FVector::DistSquared2D(Cursor, Centers[Index]);
			if (Distance < NearestDistance)
			{
				Nearest = Index;
				NearestDistance = Distance;
			}
		}
		Cursor = Centers[Nearest];
		Route.Stops.Add(Cursor);
		Centers.RemoveAt(Nearest);
	}

	Route.BuildSamples();
	return Route;
}

void FFF_FinaleSiteTourRoute::BuildSamples()
{
	Samples.Reset();
	Cumulative.Reset();
	StopDistances.Reset();
	TotalLength = 0.f;

	if (Stops.Num() == 0)
	{
		return;
	}

	Samples.Add(Stops[0]);
	Cumulative.Add(0.f);
	StopDistances.Add(0.f);

	for (int32 Segment = 0; Segment + 1 < Stops.Num(); ++Segment)
	{
		const FVector& P0 = Stops[FMath::Max(0, Segment - 1)];
		const FVector& P1 = Stops[Segment];
		const FVector& P2 = Stops[Segment + 1];
		const FVector& P3 = Stops[FMath::Min(Stops.Num() - 1, Segment + 2)];

		for (int32 Step = 1; Step <= SamplesPerSegment; ++Step)
		{
			const FVector Point = CatmullRom(P0, P1, P2, P3, static_cast<float>(Step) / SamplesPerSegment);
			TotalLength += FVector::Dist(Samples.Last(), Point);
			Samples.Add(Point);
			Cumulative.Add(TotalLength);
		}
		StopDistances.Add(TotalLength);
	}
}

FVector FFF_FinaleSiteTourRoute::NextStopAfter(const float Distance) const
{
	if (Stops.Num() == 0)
	{
		return FVector::ZeroVector;
	}
	for (int32 Index = 1; Index < Stops.Num(); ++Index)
	{
		if (StopDistances.IsValidIndex(Index) && StopDistances[Index] > Distance)
		{
			return Stops[Index];
		}
	}
	return Stops.Last();
}

FVector FFF_FinaleSiteTourRoute::Evaluate(const float Distance) const
{
	if (Samples.Num() == 0)
	{
		return FVector::ZeroVector;
	}
	if (Samples.Num() == 1 || Distance <= 0.f)
	{
		return Samples[0];
	}
	if (Distance >= TotalLength)
	{
		return Samples.Last();
	}

	// 누적 길이에서 이분 탐색으로 구간을 찾는다.
	int32 Low = 0;
	int32 High = Cumulative.Num() - 1;
	while (High - Low > 1)
	{
		const int32 Mid = (Low + High) / 2;
		if (Cumulative[Mid] <= Distance)
		{
			Low = Mid;
		}
		else
		{
			High = Mid;
		}
	}

	const float Span = Cumulative[High] - Cumulative[Low];
	const float Alpha = Span > KINDA_SMALL_NUMBER ? (Distance - Cumulative[Low]) / Span : 0.f;
	return FMath::Lerp(Samples[Low], Samples[High], Alpha);
}
