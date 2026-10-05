// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Audio/FF_AudioTypes.h"
#include "CoreMinimal.h"
#include "Building/FF_BuildModeSubsystem.h"
#include "FF_FFZSTypes.h"
#include "Finale/FF_FinaleShowTypes.h"
#include "Queue/FF_QueueTypes.h"
#include "Time/FF_FestivalStageSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "FF_AudioEventLink.generated.h"

class UFF_AudioSubsystem;
class UUserWidget;

// 우리 파트에서 나오는 신호를 사운드로 잇는다.
//
// ─── 왜 별도인가 ────────────────────────────────────────────────────────────
// 오디오 서브시스템은 "무슨 소리를 어떻게 낼지"만 안다. 어떤 순간에 소리가 나야 하는지는
// 각 파트가 알고 있고, 그 둘을 잇는 코드는 어딘가에 있어야 한다. 그 자리를 여기로 둔다.
//
// 이렇게 나눠두면 소리를 붙이려고 Queue 나 Finale 코드를 고칠 일이 없다.
// 새 소리가 필요해지면 여기에 구독 한 줄만 늘리면 된다.
//
// ─── 무엇이 없어도 돌아간다 ─────────────────────────────────────────────────
// 구독 대상이 없으면 그냥 안 붙는다. 사운드 뱅크가 비어 있어도 조용히 넘어간다.
UCLASS()
class FESTAFIESTA_API UFF_AudioEventLink : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	// 결선을 끊는다. 소리 때문에 다른 확인이 방해될 때 쓴다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	void SetLinkEnabled(bool bEnabled) { bLinkEnabled = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	bool IsLinkEnabled() const { return bLinkEnabled; }

	/* 축제 레벨에서 단계·밤낮·뱅크에 든 곡으로 틀 배경음을 정한다. 월드 없이 시험하려고 값만 받는다.

	   결산(마감·리포트)은 뱅크에 폐장 곡이 있으면 그 곡, 없으면 플레이리스트를 이어 튼다.
	   예전에는 폐장 곡이 없어도 폐장으로 갈아타서 듣던 곡만 꺼졌다. 플레이리스트로 합치며 뱅크의
	   준비·낮·밤·폐장 곡을 비웠더니 결산 화면부터 다음 날 아침까지 조용했다(피드백).
	   None 이면 지금 곡을 끈다는 뜻이다.
	*/
	static EFF_AudioAmbience ResolveStageAmbience(
		EFF_FestivalStage Stage, bool bIsNight, bool bHasPlaylist, bool bHasClosingTrack);

	/* 피날레 곡. S 등급이고 전용곡이 있으면 전용곡, 아니면(A 등급 이하) 피날레 곡.

	   연출 도중에는 바꾸지 않는다. 카메라가 순회하든 건너뛰든 끝날 때까지 같은 곡이다.
	*/
	static EFF_AudioAmbience ResolveShowAmbience(EFF_FinaleGrade Grade, bool bHasDedicatedTrack);

private:
	UFUNCTION()
	void HandleFinaleBurst(FVector WorldLocation, FLinearColor Color);

	// 바지선에서 쏘아 올릴 때. 발사 자리에서 쿵 소리를 낸다. 터지는 소리와 같이 날아오는 시간만큼 늦춘다.
	UFUNCTION()
	void HandleFinaleLaunch(FVector LaunchLocation, FVector BurstLocation, float RiseSeconds);

	// 그 자리에서 나는 소리를 날아오는 시간만큼 늦게 낸다. 터짐·발사가 같이 쓴다.
	void PlayFinaleEventWithTravelDelay(EFF_AudioEvent Event, const FVector& WorldLocation);

	UFUNCTION()
	void HandleFinaleShowStateChanged(bool bInProgress);


	/* 지금 피날레에 걸 곡. 연출 중이면 그 등급, 시작 전이면 구매한 등급으로 고른다.

	   피날레 단계에 들어서는 순간 단계 신호와 연출 시작 신호가 같은 프레임에 온다. 단계 쪽이
	   먼저 A 등급 이하 곡을 걸었다가 S 곡으로 갈아타면 첫 2초가 섞여 들린다. 그래서 단계 쪽도
	   구매한 등급을 보고 처음부터 같은 곡을 건다.
	*/
	EFF_AudioAmbience ResolveFinaleAmbience(const UFF_AudioSubsystem& Audio) const;

	// 피날레 곡이 걸려 있는지. S 전용곡도 피날레 곡이다. 단계 신호가 덮어쓰면 안 된다.
	static bool IsFinaleAmbience(EFF_AudioAmbience Ambience);

	UFUNCTION()
	void HandleQueuePartyJoined(int32 FacilityId, int32 PartyId, int32 SlotIndex, float EstimatedWaitSeconds);

	UFUNCTION()
	void HandleQueuePartyRejected(int32 FacilityId, int32 PartyId, EFF_QueueJoinResult Reason);

	UFUNCTION()
	void HandleQueueServiceStarted(int32 FacilityId, int32 PartyId, float WaitedSeconds);

	UFUNCTION()
	void HandleQueuePartyExited(int32 FacilityId, int32 PartyId, EFF_QueueExit ExitType, float WaitedSeconds);

	UFUNCTION()
	void HandleStageChanged(EFF_FestivalStage PreviousStage, EFF_FestivalStage NewStage);

	UFUNCTION()
	void HandleEnvironmentChanged(EFF_WeatherType Weather, bool bIsNight);

	UFUNCTION()
	void HandleBuildModeStateChanged(EFF_BuildModeState PreviousState, EFF_BuildModeState NewState);

	// 시설이 실제로 설치·이동·철거·개폐될 때. Zone 이 확정한 뒤에만 방송한다.
	UFUNCTION()
	void HandleWorldChanged(const FFFZSWorldChangeEvent& Event);

	// 배치가 거절됐을 때. 설치음과 다른 소리를 내야 실패한 걸 안다.
	UFUNCTION()
	void HandlePlacementRejected(const FFFZSCommand& Command, const FFFZSCommandResult& Result);

	// 지금 단계와 밤낮으로 배경음을 정한다.
	//
	// 단계만 보면 낮과 밤이 같은 곡이 되고, 밤낮만 보면 준비 단계와 운영이 구분되지 않는다.
	// 두 신호가 따로 오므로 어느 쪽이 바뀌든 여기서 다시 계산한다.
	void RefreshAmbience();

	/* 로비 레벨에서 틀 곡. 타이틀 화면이면 타이틀 곡, 그 밖의 로비 화면이면 로비 곡이다.

	   None 이면 지금 곡을 그대로 둔다는 뜻이다. 저장 기록·옵션은 타이틀과 로비 양쪽에서
	   열리므로 곡을 바꾸지 않는다. 화면이 아직 없을 때(시작 영상, 레벨 전환 직후)도 기다린다.
	*/
	EFF_AudioAmbience ResolveLobbyAmbience(const UFF_AudioSubsystem& Audio) const;

	/* 로비 화면이 바뀌었는지 본다. 바뀌었으면 곡을 다시 정한다.

	   UI 흐름이 화면 전환을 방송하지 않아 짧은 주기로 확인한다. 튜토리얼도 같은 값을
	   매 틱 읽는다. UI 쪽 코드를 고치지 않으려고 이렇게 둔다.
	*/
	void PollFrontendScreen();

	/* 플레이리스트 플레이어 위젯을 화면에 올린다. 올렸거나 올릴 수 없으면 재시도를 멈춘다.

	   축제 레벨에서만 쓴다(로비는 타이틀·로비 곡이 고정이다). 위젯은 HUD 밖에 따로 올려서
	   HUD 코드를 건드리지 않는다. 플레이어 컨트롤러가 늦게 생길 수 있어 잠깐씩 다시 시도한다.
	   보일지 말지는 위젯이 스스로 정한다(플레이리스트가 흐를 때만).
	*/
	void TryCreatePlaylistPlayer();

	void BindAll();
	void UnbindAll();

	UFF_AudioSubsystem* GetAudio() const;

	// 불꽃 소리가 듣는 자리까지 오는 데 걸리는 시간. 설정이 꺼져 있으면 0. 발사음도 같이 쓴다.
	float ResolveBurstSoundDelay(const FVector& WorldLocation) const;

	bool bLinkEnabled = true;
	bool bBoundFinale = false;
	bool bBoundQueue = false;
	bool bBoundStage = false;
	bool bBoundBuild = false;
	bool bBoundZoneWorld = false;

	// 이 월드가 로비(프론트엔드 레벨)인지. 로비면 축제 단계와 상관없이 로비 곡을 튼다.
	bool bIsLobbyWorld = false;

	// 로비 곡이 뱅크에 없다는 것을 한 번만 알린다.
	// 곡을 고르는 const 함수에서 남기므로 mutable 이다.
	mutable bool bWarnedMissingLobbyTrack = false;

	// 폐장 곡이 없어 결산 동안 플레이리스트를 잇는다는 것을 한 번만 남긴다.
	bool bWarnedMissingClosingTrack = false;

	// 로비 화면 확인 주기.
	FTimerHandle FrontendScreenPollTimer;

	// 플레이리스트 플레이어를 올릴 때까지의 재시도 주기.
	FTimerHandle PlaylistPlayerCreateTimer;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> PlaylistPlayerWidget;

	// 마지막으로 본 로비 화면(EFF_UIFlowScreen 값). 처음엔 어떤 화면과도 다르게 둔다.
	uint8 LastFrontendScreen = MAX_uint8;

	// 화면 없이 기다린 시간. 오래 비어 있으면(흐름 없이 레벨을 직접 연 경우) 로비 곡을 튼다.
	float FrontendScreenWaitSeconds = 0.f;
	bool bFrontendScreenWaitExpired = false;

	// 마지막으로 받은 상태. 두 신호를 합쳐서 배경음을 정하려면 둘 다 들고 있어야 한다.
	EFF_FestivalStage LastStage = EFF_FestivalStage::Setup;
	bool bLastIsNight = false;
};
