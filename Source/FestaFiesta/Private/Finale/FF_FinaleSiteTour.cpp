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

FVector FFF_FinaleSiteTourRoute::Extrapolate(const float Distance) const
{
	if (Samples.Num() < 2 || (Distance >= 0.f && Distance <= TotalLength))
	{
		return Evaluate(Distance);
	}

	// 끝 방향은 수평으로만 잡는다. 입구 바깥이 언덕이어도 카메라가 땅을 파고들지 않게 높이는 끝점을 따른다.
	if (Distance < 0.f)
	{
		const FVector Direction = (Samples[1] - Samples[0]).GetSafeNormal2D();
		return Samples[0] + Direction * Distance;
	}
	const FVector Direction = (Samples.Last() - Samples[Samples.Num() - 2]).GetSafeNormal2D();
	return Samples.Last() + Direction * (Distance - TotalLength);
}

void FFF_FinaleSiteTourRoute::BuildTimeline(
	const float SpeedCmPerSecond, const float StopSeconds, const float MinSeconds, const float MaxSeconds)
{
	StopTimes.Reset();
	Duration = 0.f;
	if (StopDistances.Num() == 0)
	{
		return;
	}

	// 구간마다 거리 몫과 시설 몫을 더한다.
	const float Speed = FMath::Max(1.f, SpeedCmPerSecond);
	const float PerStop = FMath::Max(0.f, StopSeconds);
	StopTimes.Add(0.f);
	for (int32 Index = 1; Index < StopDistances.Num(); ++Index)
	{
		const float Length = StopDistances[Index] - StopDistances[Index - 1];
		StopTimes.Add(StopTimes.Last() + Length / Speed + PerStop);
	}

	// 합을 범위에 맞게 같은 비율로 늘이거나 줄인다. 시설마다 받은 몫의 비율은 그대로다.
	const float Lower = FMath::Max(0.f, FMath::Min(MinSeconds, MaxSeconds));
	const float Upper = FMath::Max(Lower, MaxSeconds);
	const float Raw = StopTimes.Last();
	const float Wanted = FMath::Clamp(Raw, Lower, Upper);
	if (Raw > KINDA_SMALL_NUMBER)
	{
		const float Scale = Wanted / Raw;
		for (float& Time : StopTimes)
		{
			Time *= Scale;
		}
	}
	Duration = Wanted;
}

float FFF_FinaleSiteTourRoute::DistanceAt(const float Seconds) const
{
	if (StopTimes.Num() < 2 || Duration <= 0.f)
	{
		return Seconds > 0.f ? TotalLength : 0.f;
	}
	if (Seconds <= 0.f)
	{
		return 0.f;
	}
	if (Seconds >= Duration)
	{
		return TotalLength;
	}

	int32 Segment = 1;
	while (Segment < StopTimes.Num() - 1 && StopTimes[Segment] < Seconds)
	{
		++Segment;
	}

	/* 구간 안에서는 경유지 근처에서 느리고 가운데서 빠르다.

	   직선(일정한 속도)과 스무스스텝(양 끝에서 멈춤)을 섞는다. 완전히 멈추면 시설마다 툭툭 서서
	   끊겨 보이고, 일정하면 시설을 그냥 스쳐 간다. 섞으면 경유지에서 평균의 30% 속도로 지난다.
	*/
	constexpr float SlowAtStops = 0.7f;
	const float SegmentStart = StopTimes[Segment - 1];
	const float SegmentTime = StopTimes[Segment] - SegmentStart;
	const float Alpha = SegmentTime > KINDA_SMALL_NUMBER ? FMath::Clamp((Seconds - SegmentStart) / SegmentTime, 0.f, 1.f) : 1.f;
	const float Eased = FMath::Lerp(Alpha, FMath::SmoothStep(0.f, 1.f, Alpha), SlowAtStops);
	return FMath::Lerp(StopDistances[Segment - 1], StopDistances[Segment], Eased);
}

void FFF_FinaleSiteTourRoute::EvaluateView(
	const float FocusDistance, const float TrailCm, const float HeightCm, FVector& OutEye, FVector& OutFocus) const
{
	OutFocus = Evaluate(FocusDistance);

	// 경로를 거꾸로 거슬러 간 자리 쪽으로 물러선다. 굽은 길에서도 지나온 쪽 뒤에 선다.
	const float Trail = FMath::Max(1.f, TrailCm);
	FVector Ahead = (OutFocus - Extrapolate(FocusDistance - Trail)).GetSafeNormal2D();
	if (Ahead.IsNearlyZero())
	{
		// 경로가 점 하나다(입구뿐). 아무 방향이나 같은 결과라 X 축으로 둔다.
		Ahead = FVector::ForwardVector;
	}

	OutEye = OutFocus - Ahead * Trail + FVector(0.f, 0.f, FMath::Max(0.f, HeightCm));
}
