// FF_FinaleShellTracer.h
// 역할: 바지선에서 쏘아 올린 불꽃 알 하나의 모습(발사 번쩍임 · 오르는 빛 · 꼬리)을 그린다.

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Finale/FF_FinaleShowTypes.h"
#include "GameFramework/Actor.h"
#include "FF_FinaleShellTracer.generated.h"

class UMaterialBillboardComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UPointLightComponent;
class UStaticMeshComponent;

/* 오르는 불꽃 알 하나.

   ─── 그리기만 한다 ───────────────────────────────────────────────────────────
   언제 어디까지 오를지는 연출 서브시스템이 정한다(UFF_FinaleShowSubsystem). 서브시스템이 매
   프레임 알과 꼬리 끝 자리를 넘기고, 터질 때 Burst 를 부른다. 이 액터는 받은 자리에 그리고,
   터진 뒤 꼬리가 사라지면 스스로 없어진다. 궤적 계산이 두 곳에 있으면 터지는 자리와 꼬리가
   어긋난다.

   ─── 무엇이 없어도 돈다 ──────────────────────────────────────────────────────
   머티리얼·메시가 비어 있으면 그 부분만 빠진다. 아무것도 없어도 번쩍임 빛은 켤 수 있다.
   불꽃 데이터에 손대지 않은 레벨에서 다른 작업이 막히면 안 된다.

   ─── 안 하는 것 ─────────────────────────────────────────────────────────────
   연기는 그리지 않는다. 터지는 불꽃에 가려 거의 안 보이는데 반투명 겹침 비용이 크다.
*/
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class FESTAFIESTA_API AFF_FinaleShellTracer : public AActor
{
	GENERATED_BODY()

public:
	AFF_FinaleShellTracer();

	virtual void Tick(float DeltaSeconds) override;

	/* 발사 자리에서 준비한다. 스폰 직후 한 번 부른다.

	   bWithFlashLight 가 false 면 번쩍임의 빛(동적 조명)은 켜지 않고 빛 덩어리만 그린다.
	   동시에 켤 조명 수는 서브시스템이 센다.
	*/
	void Launch(const FFF_FinaleLaunchSpec& Spec, const FLinearColor& BurstColor, bool bWithFlashLight,
		float WobblePhase);

	// 지금 알 자리와 꼬리 끝 자리. 꼬리 끝은 알이 TrailSeconds 전에 있던 자리다.
	void UpdateFlight(const FVector& HeadLocation, const FVector& TailLocation);

	// 터졌다. 알 빛을 끄고 꼬리를 FadeOutSeconds 동안 지운 뒤 사라진다.
	void Burst();

	// 연출이 중간에 멈췄다. 기다리지 않고 바로 지운다.
	void Discard();

	// 번쩍임 빛이 켜져 있는지. 서브시스템이 동시 조명 수를 셀 때 본다.
	bool IsFlashLightActive() const;

private:
	void ApplyTrailTransform(const FVector& HeadLocation, const FVector& TailLocation);
	void SetGlowFade(float Fade);

	UPROPERTY(VisibleAnywhere, Category = "FestaFiesta|Finale")
	TObjectPtr<USceneComponent> SceneRoot;

	// 꼬리. 월드 좌표로 직접 놓는다(루트는 발사 자리에 남는다).
	UPROPERTY(VisibleAnywhere, Category = "FestaFiesta|Finale")
	TObjectPtr<UStaticMeshComponent> TrailMeshComponent;

	// 오르는 알의 빛.
	UPROPERTY(VisibleAnywhere, Category = "FestaFiesta|Finale")
	TObjectPtr<UMaterialBillboardComponent> HeadGlow;

	// 발사통 번쩍임.
	UPROPERTY(VisibleAnywhere, Category = "FestaFiesta|Finale")
	TObjectPtr<UMaterialBillboardComponent> FlashGlow;

	UPROPERTY(VisibleAnywhere, Category = "FestaFiesta|Finale")
	TObjectPtr<UPointLightComponent> FlashLight;

	// 데이터에 오르는 이펙트가 있을 때만 만든다.
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> RiseEffectComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TrailMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeadMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlashMaterialInstance;

	FFF_FinaleLaunchSpec Spec;

	// 메시의 Z 축 반길이와 중심. 꼬리 길이에 맞춰 늘릴 때 쓴다.
	float TrailMeshHalfLength = 50.f;
	float TrailMeshRadius = 50.f;
	FVector TrailMeshCenter = FVector::ZeroVector;

	float FlashElapsed = 0.f;
	float FadeElapsed = 0.f;
	bool bBurst = false;
	bool bFlashActive = false;

	// 터진 뒤 오르는 이펙트의 파티클이 사라질 때까지 기다리는 상한(초).
	float RiseEffectLinger = 0.f;
};
