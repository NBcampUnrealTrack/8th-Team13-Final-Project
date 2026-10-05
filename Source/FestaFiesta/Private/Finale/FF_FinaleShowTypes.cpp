// Copyright Epic Games, Inc. All Rights Reserved.

#include "Finale/FF_FinaleShowTypes.h"
#include "Finale/FF_FinaleShapeData.h"
#include "NiagaraSystem.h"

DEFINE_LOG_CATEGORY(LogFestaFiestaFinale);

const FFF_FinaleGradeSpec& UFF_FinaleShowData::GetSpec(EFF_FinaleGrade Grade) const
{
	switch (Grade)
	{
	case EFF_FinaleGrade::C: return GradeC;
	case EFF_FinaleGrade::B: return GradeB;
	case EFF_FinaleGrade::A: return GradeA;
	case EFF_FinaleGrade::S: return GradeS;
	default:                 return NoneGrade;
	}
}

namespace FFF_FinaleDefaults
{
	FFF_FinaleGradeSpec MakeSpec(const EFF_FinaleGrade Grade)
	{
		FFF_FinaleGradeSpec Spec;

		// 아래 숫자는 시작점이지 결론이 아니다. 화면을 보고 정해야 하는 값이라
		// 데이터 애셋으로 덮어쓸 수 있게 해뒀다.
		switch (Grade)
		{
		case EFF_FinaleGrade::S:
			Spec.BurstScale = 1.8f;
			Spec.TrailLength = 1.6f;
			Spec.BurstCount = 600;
			Spec.DurationSeconds = 90.f;
			Spec.SimultaneousShells = 6;
			Spec.SpreadRadius = 9000.f;

			// 레벨에 놓인 시점을 전부 돈다. 이게 등급 차이의 핵심이다.
			Spec.TourViewpointLimit = 0;

			// 최고 등급만 3막이다. 축제장을 한 바퀴 돌고 돌아와 일제 발사로 끝낸다.
			Spec.bSiteTour = true;
			Spec.FinalSalvoShells = 36;
			Spec.Volleys = FFF_FinaleDefaults::MakeVolleys(Grade);
			Spec.BurstHeightMin = 14000.f;
			Spec.BurstHeightMax = 34000.f;
			Spec.Colors = {
				FLinearColor(1.f, 0.25f, 0.25f), FLinearColor(1.f, 0.75f, 0.2f),
				FLinearColor(0.35f, 1.f, 0.45f), FLinearColor(0.3f, 0.6f, 1.f),
				FLinearColor(0.85f, 0.4f, 1.f),  FLinearColor(1.f, 1.f, 1.f)
			};
			break;

		case EFF_FinaleGrade::A:
			// 시점 두 곳. S 의 축소판이라 "더 움직이나" 를 궁금하게 만든다.
			Spec.TourViewpointLimit = 2;
			Spec.Volleys = FFF_FinaleDefaults::MakeVolleys(Grade);
			Spec.BurstScale = 1.5f;
			Spec.TrailLength = 1.4f;
			Spec.BurstCount = 300;
			Spec.DurationSeconds = 60.f;
			Spec.SimultaneousShells = 3;
			Spec.SpreadRadius = 7000.f;
			Spec.BurstHeightMin = 15000.f;
			Spec.BurstHeightMax = 30000.f;
			Spec.Colors = {
				FLinearColor(1.f, 0.3f, 0.3f), FLinearColor(1.f, 0.8f, 0.25f),
				FLinearColor(0.35f, 0.7f, 1.f), FLinearColor(1.f, 1.f, 1.f)
			};
			break;

		case EFF_FinaleGrade::B:
			Spec.Volleys = FFF_FinaleDefaults::MakeVolleys(Grade);
			Spec.BurstScale = 1.2f;
			Spec.TrailLength = 1.2f;
			Spec.BurstCount = 120;
			Spec.DurationSeconds = 40.f;
			Spec.SimultaneousShells = 2;
			Spec.SpreadRadius = 5000.f;
			Spec.BurstHeightMin = 16000.f;
			Spec.BurstHeightMax = 26000.f;
			Spec.Colors = {
				FLinearColor(1.f, 0.4f, 0.3f), FLinearColor(1.f, 0.85f, 0.3f),
				FLinearColor(0.4f, 0.7f, 1.f)
			};
			break;

		case EFF_FinaleGrade::C:
			Spec.BurstScale = 1.0f;
			Spec.TrailLength = 1.0f;
			Spec.BurstCount = 40;
			Spec.DurationSeconds = 12.f;
			Spec.SimultaneousShells = 1;
			Spec.SpreadRadius = 3000.f;
			Spec.BurstHeightMin = 16000.f;
			Spec.BurstHeightMax = 22000.f;
			Spec.Colors = {
				FLinearColor(1.f, 0.5f, 0.3f), FLinearColor(1.f, 0.9f, 0.4f)
			};
			break;

		default:
			/* 미구매. 아무것도 안 나온다. 예산을 못 모은 결과 자체를 보여준다.

			   BurstCount 가 0 이라 IsPlayable 이 false 고, 연출은 시작하지 않는다.
			   "그래도 한 발은" 같은 위로 연출은 두지 않는다. 안 샀으면 빈 하늘이 결과다.
			   아래 크기·색 값은 쓰이지 않지만, 칸을 비워두면 애셋 편집 화면에 0 이 떠서 남겨둔다.
			*/
			Spec.BurstScale = 0.7f;
			Spec.TrailLength = 0.8f;
			Spec.BurstCount = 0;
			Spec.DurationSeconds = 1.f;
			Spec.SimultaneousShells = 1;
			Spec.SpreadRadius = 1200.f;
			Spec.BurstHeightMin = 15000.f;
			Spec.BurstHeightMax = 18000.f;
			Spec.Colors = { FLinearColor(1.f, 0.85f, 0.6f) };
			break;
		}

		return Spec;
	}
}

namespace FFF_FinaleDefaults
{
	TArray<FFF_FinaleVolley> MakeVolleys(const EFF_FinaleGrade Grade)
	{
		TArray<FFF_FinaleVolley> Volleys;

		/* 아래 숫자는 시작점이지 결론이 아니다.
		   패턴이 몇 발을 먹고 얼마나 벌어지는지는 화면을 봐야 정해진다.
		*/
		auto Add = [&Volleys](const EFF_FinaleVolleyPattern Pattern, const int32 Count,
			const float Delay, const float Extent, const bool bUniform, const float Weight)
		{
			FFF_FinaleVolley Volley;
			Volley.Pattern = Pattern;
			Volley.ShellCount = Count;
			Volley.ShellDelaySeconds = Delay;
			Volley.ExtentCm = Extent;
			Volley.bUniformColor = bUniform;
			Volley.Weight = Weight;
			Volleys.Add(Volley);
		};

		switch (Grade)
		{
		case EFF_FinaleGrade::S:
			// 단발은 사이를 메우는 역할이라 가중치를 낮게 둔다.
			Add(EFF_FinaleVolleyPattern::Single, 1,    0.f,     0.f, false, 1.0f);
			Add(EFF_FinaleVolleyPattern::Burst,  8,    0.f,     0.f, false, 2.0f);
			Add(EFF_FinaleVolleyPattern::Line,   7,  0.08f, 14000.f,  true, 2.5f);
			Add(EFF_FinaleVolleyPattern::Fan,    9,  0.05f, 12000.f,  true, 2.0f);
			Add(EFF_FinaleVolleyPattern::Ring,  12,    0.f, 10000.f,  true, 1.5f);
			break;

		case EFF_FinaleGrade::A:
			Add(EFF_FinaleVolleyPattern::Single, 1,    0.f,     0.f, false, 2.0f);
			Add(EFF_FinaleVolleyPattern::Burst,  5,    0.f,     0.f, false, 2.0f);
			Add(EFF_FinaleVolleyPattern::Line,   5,  0.10f, 10000.f,  true, 2.0f);
			Add(EFF_FinaleVolleyPattern::Fan,    5,  0.06f,  8000.f,  true, 1.0f);
			break;

		case EFF_FinaleGrade::B:
			// 모양은 없다. 많이 터지는 것과 이펙트 종류로만 승부한다.
			Add(EFF_FinaleVolleyPattern::Single, 1,    0.f,     0.f, false, 2.0f);
			Add(EFF_FinaleVolleyPattern::Burst,  4,    0.f,     0.f, false, 1.0f);
			break;

		case EFF_FinaleGrade::C:
		default:
			// 단발만. 초라한 것 자체가 결과다.
			break;
		}

		return Volleys;
	}
}

namespace
{
	/* 모양의 지역 좌표. 경계가 -0.5 ~ +0.5 인 정규화 좌표다.

	   크기를 여기서 곱하지 않는 이유는 ExtentCm 이 이미 크기를 갖고 있기 때문이다.
	   두 곳에서 크기를 정하면 기획이 ExtentCm 을 올려도 안 커지는 일이 생긴다.
	*/
	FVector2D MakeLocalPoint(
		const EFF_FinaleVolleyPattern Pattern, const int32 Index, const int32 Count)
	{
		switch (Pattern)
		{
		case EFF_FinaleVolleyPattern::Line:
			// 한 발뿐이면 가운데.
			return FVector2D(
				(Count == 1) ? 0.f
					: (static_cast<float>(Index) / static_cast<float>(Count - 1)) - 0.5f,
				0.f);

		case EFF_FinaleVolleyPattern::Fan:
		{
			// 부챗살은 각도를 벌리되 거리는 같게 둔다. 그래야 호가 그려진다.
			const float SpreadDegrees = 120.f;
			const float Alpha = (Count == 1) ? 0.f
				: (static_cast<float>(Index) / static_cast<float>(Count - 1)) - 0.5f;
			const float Angle = FMath::DegreesToRadians(Alpha * SpreadDegrees);
			return FVector2D(FMath::Sin(Angle), FMath::Cos(Angle)) * 0.5f;
		}

		case EFF_FinaleVolleyPattern::Ring:
		{
			const float Angle = 2.f * PI * static_cast<float>(Index) / static_cast<float>(Count);
			return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 0.5f;
		}

		default:
			return FVector2D::ZeroVector;
		}
	}

	/* 지역 좌표를 월드 오프셋으로 편다.

	   바닥면은 XY 에 눕히고 Yaw 로 돌린다. 수직면은 관객을 마주보는 면에 세운다 —
	   가로축은 바라보는 방향의 오른쪽, 세로축은 그냥 위다. 수직면에 Yaw 를 적용하지
	   않는 이유는, 돌리는 순간 모양이 관객에게서 비껴서 읽히지 않기 때문이다.
	*/
	FVector LocalToWorldOffset(
		const FVector2D& Local, const EFF_FinaleShapePlane Plane,
		const FVector& FacingDirection, const float YawDegrees, const float Extent)
	{
		if (Plane == EFF_FinaleShapePlane::Vertical)
		{
			// 수평 성분만 쓴다. 관객이 언덕 위에 있어도 모양은 똑바로 서 있어야 한다.
			FVector Facing = FacingDirection.GetSafeNormal2D();
			if (Facing.IsNearlyZero())
			{
				// 관객이 모양 바로 아래위에 있다. 방향을 정할 수 없으므로 기본축을 쓴다.
				Facing = FVector::ForwardVector;
			}

			/* 관객이 보는 오른쪽. Facing 은 관객 쪽을 향하므로 관객의 시선은 -Facing 이다.

			   언리얼은 왼손 좌표계라 시선이 F 일 때 오른쪽은 Up × F 다. 여기에 -Facing 을
			   넣으면 Facing × Up 이 된다. 예전에는 Up × Facing 으로 써서 좌우가 뒤집혔다 —
			   하트·원·별은 좌우 대칭이라 안 보였을 뿐이다.
			   저작 쪽 약속은 EFF_FinaleShapeAuthorPlane::LocalXZ 주석에 있다.
			*/
			const FVector Right = FVector::CrossProduct(Facing, FVector::UpVector);

			return Right * (Local.X * Extent) + FVector::UpVector * (Local.Y * Extent);
		}

		const float Yaw = FMath::DegreesToRadians(YawDegrees);
		const float Cos = FMath::Cos(Yaw);
		const float Sin = FMath::Sin(Yaw);

		return FVector(
			(Local.X * Cos - Local.Y * Sin) * Extent,
			(Local.X * Sin + Local.Y * Cos) * Extent,
			0.f);
	}
}

void FFF_FinaleVolley::BuildShellLayout(
	const FFF_FinaleLayoutContext& Context, TArray<FVector>& OutLocations) const
{
	OutLocations.Reset();

	if (!HasFixedLayout())
	{
		// 자리를 정하지 않는 패턴이다. 호출부가 흩뿌린다.
		return;
	}

	const int32 Count = FMath::Max(1, Context.ShellCount);
	const float Extent = FMath::Max(0.f, ExtentCm);
	const EFF_FinaleShapePlane EffectivePlane = ResolvePlane();

	/* 모양 패턴은 자리를 애셋에서 가져온다.

	   여기서 동기 로드를 하는 이유는, 연출이 이미 시작된 뒤라 비동기로 기다릴
	   구간이 없기 때문이다. 애셋이 작고(점 배열뿐) 차례마다 같은 것을 쓰므로
	   두 번째부터는 캐시에서 온다.
	*/
	TArray<FVector2D> LocalPoints;

	if (Pattern == EFF_FinaleVolleyPattern::Shape)
	{
		const UFF_FinaleShapeData* ShapeData = Shape.LoadSynchronous();

		if (!ShapeData || !ShapeData->IsValidShape())
		{
			// 한 점에 몰아 쏘지 않는다. 빈 결과를 돌려주면 이 차례만 조용히 건너뛴다.
			UE_LOG(LogFestaFiestaFinale, Warning,
				TEXT("[피날레] 모양 패턴에 쓸 모양이 없다. 이번 차례는 건너뛴다. "
					 "볼리의 Shape 를 확인할 것."));
			return;
		}

		ShapeData->SamplePoints(Count, LocalPoints);

		if (LocalPoints.Num() == 0)
		{
			return;
		}
	}

	OutLocations.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		/* 모양은 애셋 순서대로 찍는다.

		   ShellDelaySeconds 가 있으면 이 순서대로 터지므로 윤곽선이 그려지듯 나온다.
		   순서를 섞으면 그 효과가 사라진다.
		*/
		const FVector2D Local = LocalPoints.IsValidIndex(Index)
			? LocalPoints[Index]
			: MakeLocalPoint(Pattern, Index, Count);

		const FVector Offset = LocalToWorldOffset(
			Local, EffectivePlane, Context.FacingDirection, Context.GroundYawDegrees, Extent);

		OutLocations.Add(Context.Center + Offset);
	}
}

UNiagaraSystem* FFF_FinaleGradeSpec::PickEffect(const uint32 Seed) const
{
	const int32 Assigned = CountAssignedEffects();
	if (Assigned == 0)
	{
		return nullptr;
	}

	// 채워진 칸 중에서 n 번째를 고른다. 빈 칸은 세지 않는다.
	int32 Target = static_cast<int32>(Seed % static_cast<uint32>(Assigned));
	for (const TSoftObjectPtr<UNiagaraSystem>& Effect : BurstEffects)
	{
		if (Effect.IsNull())
		{
			continue;
		}
		if (Target-- == 0)
		{
			return Effect.LoadSynchronous();
		}
	}

	return nullptr;
}

float FFF_FinaleLaunchSpec::ResolveRiseSeconds(const float HeightCm) const
{
	// 높이의 제곱근에 비례한다(구조체 주석). 기준이 잘못 들어와도 0 으로 나누지 않게 묶는다.
	const float Reference = FMath::Max(100.f, ReferenceHeightCm);
	const float Seconds = FMath::Max(0.1f, ReferenceRiseSeconds)
		* FMath::Sqrt(FMath::Max(0.f, HeightCm) / Reference);

	const float Low = FMath::Max(0.1f, FMath::Min(MinRiseSeconds, MaxRiseSeconds));
	const float High = FMath::Max(Low, FMath::Max(MinRiseSeconds, MaxRiseSeconds));
	return FMath::Clamp(Seconds, Low, High);
}

FVector FFF_FinaleLaunchSpec::EvaluateRise(
	const FVector& Start, const FVector& Target, const float Alpha, const float WobblePhase) const
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);

	/* 남은 비율의 거듭제곱으로 감속을 낸다. 1-(1-t)^k 는 t=0 에서 가장 빠르고 t=1 에서 멈춘다.

	   k=2 가 중력만 받은 연직 투사와 같다. 수평을 덜 감속시키면 끝에서 수평으로 조금 흘러
	   궤적이 눕는다(VerticalEase 주석).
	*/
	const float Vertical = 1.f - FMath::Pow(1.f - T, FMath::Max(1.f, VerticalEase));
	const float Horizontal = 1.f - FMath::Pow(1.f - T, FMath::Max(1.f, HorizontalEase));

	const FVector Delta = Target - Start;
	FVector Location = Start + FVector(Delta.X * Horizontal, Delta.Y * Horizontal, Delta.Z * Vertical);

	if (WobbleCm > 0.f)
	{
		/* 수평으로만 흔든다. 오르는 방향의 옆이 아니라 고정축(X·Y)에 두 번 다른 박자로 흔들어
		   곧게 오르는 발도 흔들리게 한다. sin(πt) 를 곱해 양 끝에서 0 이 되게 한다.
		*/
		const float Envelope = FMath::Sin(PI * T);
		const float SwayX = FMath::Sin(T * 2.f * PI * 1.3f + WobblePhase);
		const float SwayY = FMath::Sin(T * 2.f * PI * 0.9f + WobblePhase * 1.7f);
		Location += FVector(SwayX, SwayY, 0.f) * (WobbleCm * Envelope);
	}

	return Location;
}
