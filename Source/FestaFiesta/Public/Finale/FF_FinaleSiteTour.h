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
   입구에서 시작해 실제로 세운 시설 위를 이어 날아간다.

   ─── 흐름 ───────────────────────────────────────────────────────────────────
   해변(불꽃 관람) → 빠르게 입구로 → 시설 위를 훑으며 지나감 → 빠르게 해변으로(마무리).
   빠르게 옮겨 가는 구간은 카메라 블렌드로 낸다(FlyInSeconds · 마무리 자리의 전환 시간).

   ─── 안 하는 것 ─────────────────────────────────────────────────────────────
   시설 사이를 낮게 누비며 피해 가지는 않는다. 건물 높이가 판마다 달라 부딪히기 쉽다.
   시설들 위를 일정한 높이로 한 번 훑는다.
*/
USTRUCT(BlueprintType)
struct FFF_FinaleSiteTourSpec
{
	GENERATED_BODY()

	// 끄면 레벨·데이터에 놓인 고정 순회 자리를 쓴다(예전 방식).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour")
	bool bFollowPlacedFacilities = true;

	// 땅에서 카메라까지 높이(cm). 무대 같은 큰 시설 지붕보다 높게 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "200.0"))
	float HeightCm = 2600.f;

	/* 다음 시설이 이보다 멀면(cm) 시설 대신 경로 앞쪽을 본다. 가까워지면 다음 시설을 겨눈다.

	   경로 앞만 보면 시설 사이 빈 땅을 오래 비춘다(2026-10-05 화면 확인). 다음에 지날 시설을
	   겨누면 지날 때마다 카메라가 그쪽으로 돌아 "훑는" 느낌이 난다.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "100.0"))
	float LookAheadCm = 9000.f;

	/* 빠르게 날 때 높이를 올리는 정도(초). 높이 = max(HeightCm, 속도 × 이 값).

	   시설이 멀리 흩어져 경로가 길면 MaxSeconds 안에 끝내려고 빨라진다. 낮은 높이로 빨리 날면
	   화면이 휙휙 지나가 어지럽다. 높이를 올리면 같은 속도도 느리게 보인다.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "0.0"))
	float HeightPerSpeedSeconds = 0.6f;

	// 기본 비행 속도(cm/s). 경로가 길면 MaxSeconds 안에 끝나게 빨라진다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "100.0"))
	float SpeedCmPerSecond = 2600.f;

	// 순회 시간 범위(초). 시설이 적어도 너무 짧게 스치지 않고, 많아도 늘어지지 않게 한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "1.0"))
	float MinSeconds = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SiteTour", meta = (ClampMin = "1.0"))
	float MaxSeconds = 14.f;

	// 해변에서 입구까지 날아가는 시간(초). 짧을수록 휙 옮겨 간다.
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

/* 순회 경로. 경유지를 매끄러운 곡선으로 잇고, 거리로 위치를 꺼낸다.

   월드를 모른다. 입구와 시설 위치(땅 높이 포함)를 값으로 받아 경로만 만든다.
   그래서 월드 없이 시험할 수 있다(FF_FinaleSiteTourTests).
*/
struct FESTAFIESTA_API FFF_FinaleSiteTourRoute
{
	// 입구 다음부터 지나갈 경유지(땅 위). 0 번이 입구다.
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

	// Distance 다음에 지날 경유지(입구 제외). 다 지났으면 마지막 경유지. 경유지가 입구뿐이면 입구.
	FVector NextStopAfter(float Distance) const;

	bool IsValid() const { return Stops.Num() > 0; }

private:
	// 곡선을 잘게 나눈 점과 거기까지의 누적 길이.
	TArray<FVector> Samples;
	TArray<float> Cumulative;

	// 경유지마다 곡선 위 누적 길이. Stops 와 같은 순서다.
	TArray<float> StopDistances;
	float TotalLength = 0.f;

	void BuildSamples();
};
