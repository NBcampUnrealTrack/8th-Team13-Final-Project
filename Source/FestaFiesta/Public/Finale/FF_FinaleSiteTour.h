// FF_FinaleSiteTour.h
// 역할: 피날레 2막(축제장 순회)에서 카메라가 날아갈 경로를 만든다. 입구에서 시작해 설치된 시설을 훑는다.

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FF_FinaleSiteTour.generated.h"

/* 축제장 순회 카메라 설정. 모든 등급이 같이 쓴다(3막을 켠 등급만 순회한다).

   ─── 왜 고정 자리가 아니라 경로인가 ────────────────────────────────────────
   축제장은 판마다 다르다. 플레이어가 사흘 동안 지은 시설이 어디 있는지는 그 판에서만 안다.
   고정 자리로 찍으면 빈 땅이나 벽을 비춘다(2026-10-05 화면 확인). 그래서 손님이 들어오는
   입구에서 시작해 실제로 세운 시설을 차례로 지난다.

   ─── 흐름 ───────────────────────────────────────────────────────────────────
   해변(불꽃 관람) → 빠르게 입구 바깥으로 → 입구로 들어오며 시설을 하나씩 비춤 → 빠르게 해변으로(마무리).
   빠르게 옮겨 가는 구간은 카메라 블렌드로 낸다(FlyInSeconds · 마무리 자리의 전환 시간).

   ─── 보는 지점을 경로에 태우고 카메라는 뒤에서 따라간다 ───────────────────
   예전에는 카메라가 경로 위를 날며 다음 시설을 겨눴다. 입구 바로 위에서 출발하니 입구 옆 건물은
   출발부터 카메라 발밑·뒤에 있어 한 번도 화면에 안 잡혔다(2026-10-05 피드백 "입구쪽 건물이 짤림").
   지금은 보는 지점이 입구에서 출발해 시설 하나하나를 지나고, 카메라는 그 지점 뒤로 TrailCm ·
   위로 HeightCm 떨어져 따라간다. 그래서 출발 자리는 입구 바깥이고, 모든 시설이 한 번씩 화면
   가운데에 온다. 보는 지점은 시설 근처에서 느려진다(StopSeconds).

   ─── 안 하는 것 ─────────────────────────────────────────────────────────────
   시설 사이를 낮게 누비며 피해 가지는 않는다. 건물 높이가 판마다 달라 부딪히기 쉽다.
   일정한 높이에서 내려다보며 지난다.
*/
USTRUCT(BlueprintType)
struct FFF_FinaleSiteTourSpec
{
	GENERATED_BODY()

	// 끄면 레벨·데이터에 놓인 고정 순회 자리를 쓴다(예전 방식).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour")
	bool bFollowPlacedFacilities = true;

	// 보는 지점(땅)에서 카메라까지 높이(cm). 무대 같은 큰 시설 지붕보다 높게 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "200.0"))
	float HeightCm = 2600.f;

	/* 카메라가 보는 지점에서 뒤로 떨어진 거리(cm). 높이와 함께 내려다보는 각도를 정한다.

	   2600 · 4000 이면 33도쯤 내려다본다. 짧으면 시설을 바로 위에서 보고, 길면 멀어서 작게 보인다.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "100.0"))
	float TrailCm = 4000.f;

	/* 빠르게 날 때 높이를 올리는 정도(초). 높이 = max(HeightCm, 평균 속도 × 이 값).

	   시설이 멀리 흩어져 경로가 길면 MaxSeconds 안에 끝내려고 빨라진다. 낮은 높이로 빨리 날면
	   화면이 휙휙 지나가 어지럽다. 높이를 올리면 같은 속도도 느리게 보인다. 뒤로 떨어진 거리도
	   같은 비율로 늘려 내려다보는 각도는 그대로 둔다.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "0.0"))
	float HeightPerSpeedSeconds = 0.6f;

	// 기본 비행 속도(cm/s). 경로가 길면 MaxSeconds 안에 끝나게 빨라진다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "100.0"))
	float SpeedCmPerSecond = 2600.f;

	/* 시설 하나마다 더 쓰는 시간(초). 보는 지점이 시설 근처에서 느려지는 몫이다.

	   거리로만 시간을 나누면 붙어 있는 시설은 순식간에 지나간다. 입구 옆 건물이 그랬다.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "0.0"))
	float StopSeconds = 1.2f;

	// 순회 시간 범위(초). 시설이 적어도 너무 짧게 스치지 않고, 많아도 늘어지지 않게 한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "1.0"))
	float MinSeconds = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "1.0"))
	float MaxSeconds = 14.f;

	// 해변에서 입구 바깥까지 날아가는 시간(초). 짧을수록 휙 옮겨 간다. 이 동안 순회는 출발점에서 기다린다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "0.0"))
	float FlyInSeconds = 1.8f;

	// 시설을 이 크기(cm)의 칸으로 묶어 한 번씩만 지나간다. 부스가 붙어 있으면 한 덩어리로 본다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "100.0"))
	float ClusterCm = 3500.f;

	// 경유지 수 상한. 넘치면 칸을 키워 묶는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "1"))
	int32 MaxStops = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "10.0", ClampMax = "150.0"))
	float FieldOfView = 75.f;
};

/* 순회 경로. 경유지를 매끄러운 곡선으로 잇고, 거리 · 시간으로 위치를 꺼낸다.

   월드를 모른다. 입구와 시설 위치(땅 높이 포함)를 값으로 받아 경로만 만든다.
   그래서 월드 없이 시험할 수 있다(FF_FinaleSiteTourTests).
*/
struct FESTAFIESTA_API FFF_FinaleSiteTourRoute
{
	// 지나갈 경유지(땅 위). 0 번이 입구, 나머지는 시설 묶음이다.
	TArray<FVector> Stops;

	/* 경유지를 칸으로 묶고 입구에서 가까운 순서로 잇는다.

	   가장 가까운 것부터 이어 가는 단순한 순서다. 최단 경로는 아니지만 되돌아가는 일이 적고,
	   무엇보다 매번 같은 배치에서 같은 경로가 나온다.
	*/
	static FFF_FinaleSiteTourRoute Build(const FVector& Entrance, const TArray<FVector>& Sites,
		float ClusterCm, int32 MaxStops);

	// 경유지를 지나는 곡선의 길이(cm). 경유지가 하나뿐이면 0.
	float GetLength() const { return TotalLength; }

	// 곡선 위 Distance 지점(땅 높이 기준). 범위 밖은 양 끝에 묶는다.
	FVector Evaluate(float Distance) const;

	// Evaluate 와 같되 범위 밖은 양 끝의 방향으로 곧게 늘인다. 입구 바깥 자리를 잴 때 쓴다.
	FVector Extrapolate(float Distance) const;

	// 경유지 Index 의 곡선 위 누적 길이. 범위 밖이면 0.
	float GetStopDistance(int32 Index) const { return StopDistances.IsValidIndex(Index) ? StopDistances[Index] : 0.f; }

	/* 보는 지점이 경로를 지나는 시간표를 만든다.

	   구간(경유지 → 다음 경유지)마다 "길이 / 속도 + StopSeconds" 를 쓰고, 합을 [MinSeconds, MaxSeconds] 에
	   맞게 같은 비율로 늘이거나 줄인다. 구간 안에서는 양 끝(경유지)에서 느리고 가운데서 빠르다.
	*/
	void BuildTimeline(float SpeedCmPerSecond, float StopSeconds, float MinSeconds, float MaxSeconds);

	// 시간표의 전체 시간(초). BuildTimeline 전이면 0.
	float GetDuration() const { return Duration; }

	// 순회를 시작하고 Seconds 가 지났을 때 보는 지점의 곡선 위 거리. 0 → 0, 끝 → 길이.
	float DistanceAt(float Seconds) const;

	// 경유지 Index 에 보는 지점이 닿는 시각(초). 범위 밖이면 0.
	float GetStopTime(int32 Index) const { return StopTimes.IsValidIndex(Index) ? StopTimes[Index] : 0.f; }

	/* 보는 지점이 FocusDistance 에 있을 때의 카메라 자리.

	   카메라는 보는 지점에서 경로를 거꾸로 TrailCm 만큼 거슬러 간 방향으로 수평 TrailCm, 위로 HeightCm
	   떨어진다. 출발(0)에서는 입구 바깥에 선다. 카메라 방향은 OutEye → OutFocus 다.
	*/
	void EvaluateView(float FocusDistance, float TrailCm, float HeightCm, FVector& OutEye, FVector& OutFocus) const;

	bool IsValid() const { return Stops.Num() > 0; }

private:
	// 곡선을 잘게 나눈 점과 거기까지의 누적 길이.
	TArray<FVector> Samples;
	TArray<float> Cumulative;

	// 경유지마다 곡선 위 누적 길이와 보는 지점이 닿는 시각. Stops 와 같은 순서다.
	TArray<float> StopDistances;
	TArray<float> StopTimes;
	float TotalLength = 0.f;
	float Duration = 0.f;

	void BuildSamples();
};
