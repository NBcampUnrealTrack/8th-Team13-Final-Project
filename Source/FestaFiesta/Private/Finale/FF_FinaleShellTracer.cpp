// FF_FinaleShellTracer.cpp
// 역할: 바지선에서 쏘아 올린 불꽃 알 하나의 모습(발사 번쩍임 · 오르는 빛 · 꼬리)을 그린다.

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Finale/FF_FinaleShellTracer.h"

#include "Components/MaterialBillboardComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
	// 터진 뒤 오르는 이펙트를 기다리는 상한. 파티클 수명이 이보다 길어도 여기서 지운다.
	constexpr float RiseEffectLingerSeconds = 2.5f;

	// 꼬리가 이보다 짧으면 그리지 않는다. 0 길이로 늘리면 메시가 한 점에 뭉쳐 깜빡인다.
	constexpr float MinTrailLengthCm = 10.f;
}

AFF_FinaleShellTracer::AFF_FinaleShellTracer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// 레벨 저장·네트워크와 무관한 일회용 모습이다.
	SetReplicates(false);
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TrailMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Trail"));
	TrailMeshComponent->SetupAttachment(SceneRoot);
	TrailMeshComponent->SetUsingAbsoluteLocation(true);
	TrailMeshComponent->SetUsingAbsoluteRotation(true);
	TrailMeshComponent->SetUsingAbsoluteScale(true);
	TrailMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TrailMeshComponent->SetCastShadow(false);
	TrailMeshComponent->SetCanEverAffectNavigation(false);
	TrailMeshComponent->SetGenerateOverlapEvents(false);
	TrailMeshComponent->SetVisibility(false);

	HeadGlow = CreateDefaultSubobject<UMaterialBillboardComponent>(TEXT("HeadGlow"));
	HeadGlow->SetupAttachment(SceneRoot);
	HeadGlow->SetUsingAbsoluteLocation(true);
	HeadGlow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeadGlow->SetCastShadow(false);
	HeadGlow->SetVisibility(false);

	FlashGlow = CreateDefaultSubobject<UMaterialBillboardComponent>(TEXT("FlashGlow"));
	FlashGlow->SetupAttachment(SceneRoot);
	FlashGlow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlashGlow->SetCastShadow(false);
	FlashGlow->SetVisibility(false);

	FlashLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashLight"));
	FlashLight->SetupAttachment(SceneRoot);
	FlashLight->SetCastShadows(false);
	FlashLight->SetIntensityUnits(ELightUnits::Candelas);
	FlashLight->SetIntensity(0.f);
	FlashLight->SetVisibility(false);
}

void AFF_FinaleShellTracer::Launch(
	const FFF_FinaleLaunchSpec& InSpec, const FLinearColor& BurstColor, const bool bWithFlashLight,
	const float WobblePhase)
{
	Spec = InSpec;
	const FVector Origin = GetActorLocation();
	const FLinearColor TrailColor = Spec.bTrailUsesBurstColor ? BurstColor : Spec.TrailColor;

	// ── 꼬리 ──
	UStaticMesh* Mesh = Spec.TrailMesh.LoadSynchronous();
	UMaterialInterface* TrailMaterial = Spec.TrailMaterial.LoadSynchronous();
	if (Mesh && TrailMaterial)
	{
		// 메시마다 크기와 중심이 달라서 경계 상자로 늘린다. 기본 원뿔은 지름·높이 100 이다.
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		TrailMeshHalfLength = FMath::Max(1.f, static_cast<float>(Bounds.BoxExtent.Z));
		TrailMeshRadius = FMath::Max(1.f, static_cast<float>(FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y)));
		TrailMeshCenter = Bounds.Origin;

		TrailMeshComponent->SetStaticMesh(Mesh);
		TrailMaterialInstance = UMaterialInstanceDynamic::Create(TrailMaterial, this);
		TrailMeshComponent->SetMaterial(0, TrailMaterialInstance);
		TrailMaterialInstance->SetVectorParameterValue(FFF_FinaleLaunchParams::Color, TrailColor);
		TrailMaterialInstance->SetScalarParameterValue(FFF_FinaleLaunchParams::Intensity, Spec.TrailIntensity);
		TrailMaterialInstance->SetScalarParameterValue(FFF_FinaleLaunchParams::Fade, 1.f);
	}

	// ── 알 빛과 번쩍임 ──
	if (UMaterialInterface* GlowMaterial = Spec.GlowMaterial.LoadSynchronous())
	{
		HeadMaterialInstance = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		HeadMaterialInstance->SetVectorParameterValue(FFF_FinaleLaunchParams::Color, TrailColor);
		HeadMaterialInstance->SetScalarParameterValue(FFF_FinaleLaunchParams::Intensity, Spec.TrailIntensity);
		HeadMaterialInstance->SetScalarParameterValue(FFF_FinaleLaunchParams::Fade, 1.f);
		HeadMaterialInstance->SetScalarParameterValue(FFF_FinaleLaunchParams::Phase, WobblePhase);
		HeadGlow->AddElement(HeadMaterialInstance, nullptr, false, Spec.HeadSizeCm, Spec.HeadSizeCm, nullptr);
		HeadGlow->SetWorldLocation(Origin);
		HeadGlow->SetVisibility(true);

		if (Spec.FlashSeconds > 0.f && Spec.FlashGlowSizeCm > 0.f)
		{
			FlashMaterialInstance = UMaterialInstanceDynamic::Create(GlowMaterial, this);
			FlashMaterialInstance->SetVectorParameterValue(FFF_FinaleLaunchParams::Color, Spec.FlashColor);
			FlashMaterialInstance->SetScalarParameterValue(FFF_FinaleLaunchParams::Intensity, Spec.TrailIntensity * 2.f);
			FlashMaterialInstance->SetScalarParameterValue(FFF_FinaleLaunchParams::Fade, 1.f);
			FlashMaterialInstance->SetScalarParameterValue(FFF_FinaleLaunchParams::Phase, WobblePhase);
			FlashGlow->AddElement(FlashMaterialInstance, nullptr, false,
				Spec.FlashGlowSizeCm, Spec.FlashGlowSizeCm, nullptr);
			FlashGlow->SetVisibility(true);
		}
	}

	bFlashActive = Spec.FlashSeconds > 0.f;
	if (bFlashActive && bWithFlashLight && Spec.FlashLightIntensity > 0.f)
	{
		FlashLight->SetLightColor(Spec.FlashColor);
		FlashLight->SetAttenuationRadius(Spec.FlashRadiusCm);
		FlashLight->SetIntensity(Spec.FlashLightIntensity);
		FlashLight->SetVisibility(true);
	}

	// ── 오르는 이펙트 ──
	if (UNiagaraSystem* RiseEffect = Spec.RiseEffect.LoadSynchronous())
	{
		RiseEffectComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, RiseEffect, Origin, FRotator::ZeroRotator, FVector::OneVector,
			false, true, ENCPoolMethod::None, true);
		if (RiseEffectComponent)
		{
			RiseEffectComponent->SetColorParameter(FFF_FinaleParams::BurstColor, BurstColor);
		}
	}
}

void AFF_FinaleShellTracer::UpdateFlight(const FVector& HeadLocation, const FVector& TailLocation)
{
	if (bBurst)
	{
		return;
	}

	HeadGlow->SetWorldLocation(HeadLocation);
	ApplyTrailTransform(HeadLocation, TailLocation);

	if (RiseEffectComponent)
	{
		RiseEffectComponent->SetWorldLocation(HeadLocation);
	}
}

void AFF_FinaleShellTracer::ApplyTrailTransform(const FVector& HeadLocation, const FVector& TailLocation)
{
	if (!TrailMaterialInstance)
	{
		return;
	}

	const FVector Along = TailLocation - HeadLocation;
	const float Length = Along.Size();
	if (Length < MinTrailLengthCm)
	{
		TrailMeshComponent->SetVisibility(false);
		return;
	}

	/* 메시의 +Z 를 꼬리 끝, -Z 를 알 쪽으로 놓는다. 원뿔은 -Z 쪽이 밑면이라 알 쪽이 굵다.

	   길이 방향(Z)만 꼬리 길이에 맞추고 굵기는 그대로 둔다. 회전은 Z 축을 꼬리 방향에 맞춘다.
	*/
	const FVector Direction = Along / Length;
	const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Direction);
	const FVector Scale(
		Spec.TrailWidthCm / (2.f * TrailMeshRadius),
		Spec.TrailWidthCm / (2.f * TrailMeshRadius),
		Length / (2.f * TrailMeshHalfLength));

	// 메시 중심이 원점이 아닐 수 있어 중심을 빼고 놓는다.
	const FVector Midpoint = (HeadLocation + TailLocation) * 0.5f;
	const FVector Location = Midpoint - Rotation.RotateVector(TrailMeshCenter * Scale);

	TrailMeshComponent->SetWorldLocationAndRotation(Location, Rotation);
	TrailMeshComponent->SetWorldScale3D(Scale);
	TrailMeshComponent->SetVisibility(true);

	TrailMaterialInstance->SetVectorParameterValue(FFF_FinaleLaunchParams::HeadPosition, FLinearColor(HeadLocation));
	TrailMaterialInstance->SetScalarParameterValue(FFF_FinaleLaunchParams::TrailLength, Length);
}

void AFF_FinaleShellTracer::Burst()
{
	if (bBurst)
	{
		return;
	}

	bBurst = true;
	FadeElapsed = 0.f;
	HeadGlow->SetVisibility(false);

	// 남은 파티클은 저절로 사라지게 둔다. 바로 지우면 꼬리가 툭 끊긴다.
	if (RiseEffectComponent)
	{
		RiseEffectComponent->Deactivate();
		RiseEffectLinger = RiseEffectLingerSeconds;
	}
}

void AFF_FinaleShellTracer::Discard()
{
	if (RiseEffectComponent)
	{
		RiseEffectComponent->DestroyComponent();
		RiseEffectComponent = nullptr;
	}
	Destroy();
}

bool AFF_FinaleShellTracer::IsFlashLightActive() const
{
	return bFlashActive && FlashLight && FlashLight->IsVisible();
}

void AFF_FinaleShellTracer::SetGlowFade(const float Fade)
{
	if (FlashMaterialInstance)
	{
		FlashMaterialInstance->SetScalarParameterValue(FFF_FinaleLaunchParams::Fade, Fade);
	}
}

void AFF_FinaleShellTracer::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// ── 번쩍임: 확 켜졌다가 빠르게 꺼진다(제곱으로 줄여 끝이 끌리지 않게) ──
	if (bFlashActive)
	{
		FlashElapsed += DeltaSeconds;
		const float Remaining = Spec.FlashSeconds > 0.f
			? FMath::Clamp(1.f - FlashElapsed / Spec.FlashSeconds, 0.f, 1.f)
			: 0.f;
		const float Strength = Remaining * Remaining;

		FlashLight->SetIntensity(Spec.FlashLightIntensity * Strength);
		SetGlowFade(Strength);

		if (Remaining <= 0.f)
		{
			bFlashActive = false;
			FlashLight->SetVisibility(false);
			FlashGlow->SetVisibility(false);
		}
	}

	if (!bBurst)
	{
		return;
	}

	// ── 터진 뒤: 꼬리를 지우고, 번쩍임과 이펙트까지 끝나면 사라진다 ──
	FadeElapsed += DeltaSeconds;
	const float Fade = Spec.FadeOutSeconds > 0.f
		? FMath::Clamp(1.f - FadeElapsed / Spec.FadeOutSeconds, 0.f, 1.f)
		: 0.f;
	if (TrailMaterialInstance)
	{
		TrailMaterialInstance->SetScalarParameterValue(FFF_FinaleLaunchParams::Fade, Fade);
	}
	if (Fade <= 0.f)
	{
		TrailMeshComponent->SetVisibility(false);
	}

	if (RiseEffectComponent)
	{
		RiseEffectLinger -= DeltaSeconds;
		if (RiseEffectLinger <= 0.f || !RiseEffectComponent->IsActive())
		{
			RiseEffectComponent->DestroyComponent();
			RiseEffectComponent = nullptr;
		}
	}

	if (Fade <= 0.f && !bFlashActive && !RiseEffectComponent)
	{
		Destroy();
	}
}
