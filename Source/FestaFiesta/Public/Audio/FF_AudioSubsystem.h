// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Audio/FF_AudioTypes.h"
#include "CoreMinimal.h"
#include "Debug/FF_DebugConsole.h"
#include "Subsystems/WorldSubsystem.h"
#include "FF_AudioSubsystem.generated.h"

class UAudioComponent;
class UFF_AudioBankData;

// 플레이리스트 상태가 바뀌었다(곡·일시정지·자동 다음 곡·셔플, 플레이리스트가 켜지거나 꺼짐).
// 플레이어 UI 가 다시 그리는 데 쓴다. 재생 위치는 매 프레임 바뀌므로 여기로 알리지 않는다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFF_OnPlaylistChanged);

// 축제 전반의 소리를 담당한다.
//
// ─── 두 갈래 ────────────────────────────────────────────────────────────────
// 원샷    불꽃 터짐, 클릭음. 한 번 울리고 사라진다.
// 배경음  계속 흐르고 상황이 바뀌면 크로스페이드로 갈아탄다.
//
// 둘을 한 함수로 묶지 않는 이유는 수명이 다르기 때문이다. 원샷은 재생 후 잊어도 되지만
// 배경음은 컴포넌트를 들고 있다가 다음 곡으로 넘겨줘야 한다.
//
// ─── 애셋이 없어도 돌아간다 ─────────────────────────────────────────────────
// 사운드 뱅크가 비어 있으면 조용히 넘어간다. 크래시도 경고 폭탄도 없다.
// 소리가 안 붙은 상태에서 다른 파트 작업이 막히면 안 된다.
//
// ─── 볼륨 ───────────────────────────────────────────────────────────────────
// 볼륨 값은 여기서 소유하지 않는다. 플레이어 개인 설정(UFF_RuntimeSettingsSubsystem)이 단일 소유자다.
// 마스터는 그쪽이 엔진 전체에 걸고, 배경음·효과음 비율은 여기서 재생할 때 곱한다.
// 사운드 클래스로 가르지 않은 이유는 애셋을 만들고 모든 소리에 지정하기 전에도 슬라이더가 먹어야 해서다.
// 그래서 이 서브시스템을 거치지 않는 소리(버튼 스타일에 박힌 클릭음 등)는 마스터만 따른다.
UCLASS()
class FESTAFIESTA_API UFF_AudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UFF_AudioSubsystem* Get(const UObject* WorldContextObject);

	// ── 원샷 ────────────────────────────────────────────────────────────────

	// 화면 전체에 들리는 소리. UI 나 전역 알림에 쓴다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	bool PlayEvent2D(EFF_AudioEvent Event);

	// 특정 위치에서 나는 소리. 거리에 따라 작아진다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	bool PlayEventAtLocation(EFF_AudioEvent Event, const FVector& WorldLocation);

	// ── 배경음 ──────────────────────────────────────────────────────────────

	// 배경음을 갈아탄다. 같은 것을 다시 요청하면 아무것도 하지 않는다.
	//
	// 재요청을 무시하는 게 중요하다. 매 틱 상태를 확인해서 호출하는 코드가 붙으면
	// 그때마다 처음부터 다시 재생되어 소리가 계속 끊긴다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	void SetAmbience(EFF_AudioAmbience Ambience);

	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	void StopAmbience();

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	EFF_AudioAmbience GetCurrentAmbience() const { return CurrentAmbience; }

	// 뱅크에 이 상황의 곡이 들어 있는지.
	// 없는 곡으로 갈아타면 지금 곡만 꺼지고 조용해진다. 그걸 피하고 싶은 호출부가 먼저 본다.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	bool HasAmbienceTrack(EFF_AudioAmbience Ambience) const;

	// ── 플레이리스트 ────────────────────────────────────────────────────────
	//
	// SetAmbience(Playlist) 로 켠다. 다른 배경음으로 갈아타면 듣던 곡과 위치를 기억해 두고,
	// 다시 Playlist 로 돌아오면 그 자리부터 잇는다. 곡이 끝나면 다음 곡으로 넘어간다.

	// 뱅크의 플레이리스트에 틀 수 있는 곡이 하나라도 있는지.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	bool HasPlaylist() const;

	// 플레이리스트가 흐르는 중이면 다음 곡으로 넘긴다. 아니면 아무것도 안 한다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	void SkipPlaylistTrack();

	// 지금 플레이리스트가 걸려 있는지. 플레이어 UI 를 띄울지 정한다.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	bool IsPlaylistActive() const { return CurrentAmbience == EFF_AudioAmbience::Playlist; }

	// 뱅크 플레이리스트의 곡 수. 빈 칸도 센다(목록 번호가 뱅크 순서와 같아야 해서다).
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	int32 GetPlaylistTrackCount() const;

	// 곡 이름. 뱅크에 이름이 없으면 사운드 애셋 이름에서 만든다. TrackIndex 는 뱅크 목록 순번이다.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	FText GetPlaylistTrackDisplayName(int32 TrackIndex) const;

	// 지금 곡의 뱅크 목록 순번. 없으면 INDEX_NONE.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	int32 GetCurrentPlaylistTrack() const;

	// 목록에서 고른 곡을 처음부터 튼다. TrackIndex 는 뱅크 목록 순번이다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	void PlayPlaylistTrack(int32 TrackIndex);

	// 이전 곡. 지금 곡을 몇 초 이상 들었으면 지금 곡의 처음으로 간다(흔한 음악 플레이어 규칙).
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	void PreviousPlaylistTrack();

	// 플레이리스트만 멈추거나 잇는다. 게임 일시정지와 별개다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	void SetPlaylistPaused(bool bPaused);

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	bool IsPlaylistPaused() const { return bPlaylistPaused; }

	// 켜면 곡이 끝날 때 다음 곡, 끄면 지금 곡을 처음부터 다시 튼다. 설정은 저장된다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	void SetPlaylistAutoAdvance(bool bAutoAdvance);

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	bool IsPlaylistAutoAdvance() const { return bPlaylistAutoAdvance; }

	// 재생 순서를 섞는다. 지금 곡은 끊지 않는다. 설정은 저장된다.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	void SetPlaylistShuffle(bool bShuffle);

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	bool IsPlaylistShuffle() const { return bPlaylistShuffle; }

	// 지금 곡에서 들은 위치(초). 플레이리스트가 아니면 마지막으로 남긴 위치다.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	float GetPlaylistPlaybackSeconds() const;

	// 지금 곡 길이(초). 모르면 0.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	float GetPlaylistTrackLength() const { return PlaylistTrackLength; }

	UPROPERTY(BlueprintAssignable, Category = "FestaFiesta|Audio")
	FFF_OnPlaylistChanged OnPlaylistChanged;

	/* 갈래별 개인 설정 비율(0~1). 마스터는 빠져 있다. 마스터는 엔진이 이미 곱한다.

	   배경음·효과음·불꽃놀이·UI 는 설정 창의 같은 이름 슬라이더를 따른다.
	   환경음은 아직 소리가 없어 효과음 슬라이더에 묶는다.
	*/
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	float GetChannelVolume(EFF_AudioChannel Channel) const;

	/* 이 소리가 따를 갈래. 뱅크에서 고른 값이 먼저다.

	   기본값(효과음)으로 둔 소리만 이벤트 종류로 가른다. 피날레 이벤트는 불꽃놀이,
	   UI 이벤트는 UI 로 보낸다. 뱅크의 칸을 일일이 고치지 않아도 슬라이더가 먹게 하려는 것이다.
	*/
	static EFF_AudioChannel ResolveChannel(EFF_AudioEvent Event, const FFF_AudioCue& Cue);

	// ── 진단 ────────────────────────────────────────────────────────────────

	// 소리를 전부 끈다. 다른 파트를 확인할 때 방해되지 않게.
	UFUNCTION(BlueprintCallable, Category = "FestaFiesta|Audio")
	void SetAudioEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	bool IsAudioEnabled() const { return bAudioEnabled; }

	// 재생한 횟수. 0 에서 안 올라가면 이벤트가 안 들어오고 있는 것이다.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	int32 GetPlayedCueCount() const { return PlayedCues; }

	// 뱅크에 항목이 없어서 넘어간 횟수. 이게 크면 뱅크를 채워야 한다.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	int32 GetMissingCueCount() const { return MissingCues; }

	// 연타 제한에 걸려 무시된 횟수.
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	int32 GetSuppressedCueCount() const { return SuppressedCues; }

	// 배경음 곡을 새로 튼 횟수. 같은 곡으로 이름만 바뀔 때는 늘지 않는다(끊김 진단).
	UFUNCTION(BlueprintPure, Category = "FestaFiesta|Audio")
	int32 GetStartedAmbienceTrackCount() const { return StartedAmbienceTracks; }

	// 직접 지정한 뱅크. 비워두면 GameMode → Project Settings 순으로 찾는다.
	UPROPERTY(BlueprintReadWrite, Category = "FestaFiesta|Audio")
	TSoftObjectPtr<UFF_AudioBankData> BankOverride;

private:
	// 쓸 뱅크를 고른다. 직접 지정 → GameMode → Project Settings 순.
	const UFF_AudioBankData* ResolveBank() const;

	// 재생 직전 검사. 꺼져 있거나, 항목이 없거나, 연타면 false.
	bool CanPlay(EFF_AudioEvent Event, const FFF_AudioCue*& OutCue);

	// 여러 개 중 하나를 고른다. 같은 소리가 반복되면 금방 질린다.
	USoundBase* PickSound(const FFF_AudioCue& Cue) const;

	void RegisterDebugConsoleCommands();
	void UnregisterDebugConsoleCommands();

	// 설정 창에서 저장하면 온다. 이미 흐르는 배경음에 새 비율을 건다.
	// 원샷은 다음 재생부터 새 값을 읽으므로 따로 할 일이 없다.
	UFUNCTION()
	void HandleRuntimeSettingsChanged();

	void BindRuntimeSettings();
	void UnbindRuntimeSettings();

	/* 배경음이 끝까지 재생됐다. 반복할 곡이면 처음부터 다시 튼다.

	   반복은 원래 사운드 애셋의 Looping 체크로 정한다. 그런데 곡을 새로 넣을 때마다
	   그 칸을 빠뜨려서 타이틀·준비 곡이 한 번 돌고 끊겼다(피드백). 배경음은 엔딩곡을
	   빼면 전부 반복이 맞으므로 여기서 보장한다. 애셋이 이미 반복이면 끝나지 않아
	   이 함수가 불리지 않는다.
	*/
	void HandleAmbienceFinished(UAudioComponent* Component);

	// 지금 배경음을 페이드아웃 칸으로 옮긴다. 갈아탈 때와 플레이리스트 곡을 넘길 때 같이 쓴다.
	void RetireAmbienceComponent(float FadeOutSeconds);

	// 곡 하나를 지금 배경음으로 튼다. StartSeconds 부터 재생한다.
	void SpawnAmbienceComponent(USoundBase* Sound, const FFF_AudioAmbienceTrack& Track, float StartSeconds);

	// 지금 걸린 배경음의 곡 설정. 플레이리스트면 지금 곡이다. 없으면 nullptr.
	const FFF_AudioAmbienceTrack* FindActiveTrack(const UFF_AudioBankData* Bank) const;

	/* 플레이리스트의 지금 곡을 기억한 위치부터 튼다. 못 틀었으면 false.

	   빈 칸은 건너뛴다. 기억한 위치가 곡 끝에 거의 닿았으면 다음 곡의 처음부터 튼다.
	   남은 몇 초만 틀고 바로 넘어가면 끊긴 것처럼 들린다.
	*/
	bool StartPlaylistTrack();

	// 다음 곡으로 넘어간다. 곡이 끝났을 때와 SkipPlaylistTrack 이 부른다.
	void AdvancePlaylist();

	// 곡이 끝났다. 자동 다음 곡이면 다음 곡, 아니면 같은 곡을 처음부터.
	void HandlePlaylistTrackEnded();

	/* 재생 순서의 Cursor 번째 곡을 처음부터 튼다. 지금 곡은 페이드아웃시킨다.

	   bReshuffle 이면 지금 곡을 내린 뒤 순서를 새로 섞는다(한 바퀴를 돈 경우).
	   AvoidFirstTrack 은 섞을 때 첫 곡이 되지 않게 할 곡이다.
	*/
	void RestartPlaylistAt(int32 Cursor, bool bReshuffle = false, int32 AvoidFirstTrack = INDEX_NONE);

	// 자동 다음 곡·셔플 설정을 읽고 쓴다. 게임 사용자 설정 파일(GameUserSettings.ini)에 남긴다.
	void LoadPlaylistPreferences();
	void SavePlaylistPreferences() const;

	// 지금 곡에서 들은 위치를 남긴다. 플레이리스트를 떠나기 직전에 부른다.
	void CapturePlaylistPosition();

	// 재생 순서를 만든다. 섞기가 켜져 있으면 섞되, 직전 곡이 첫 곡이 되지 않게 한다.
	void RebuildPlaylistOrder(int32 TrackCount, bool bShuffle, int32 AvoidFirstTrack);

	bool bAudioEnabled = true;

	// 지금 배경음의 곡 자체 볼륨. 설정이 바뀌면 여기에 비율을 다시 곱한다.
	float AmbienceTrackVolume = 1.f;

	EFF_AudioAmbience CurrentAmbience = EFF_AudioAmbience::None;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbienceComponent;

	// 이전 배경음. 크로스페이드가 끝날 때까지 들고 있다가 정리한다.
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> FadingAmbienceComponent;

	FTimerHandle FadingCleanupTimer;

	// ── 플레이리스트 상태 ──

	// 재생 순서(뱅크 목록의 번호)와 그중 지금 위치.
	TArray<int32> PlaylistOrder;
	int32 PlaylistCursor = 0;

	// 다시 돌아왔을 때 이어 틀 위치(초). 곡이 바뀌면 0 이다.
	float PlaylistResumeSeconds = 0.f;

	/* 지금 곡을 튼 순간의 오디오 시각과 시작 위치. 들은 위치를 계산한다.

	   오디오 시각(GetAudioTimeSeconds)은 게임이 멈추면 같이 멈춘다. 배경음도 멈추므로
	   일시정지 동안 위치가 앞서 가지 않는다.
	*/
	double PlaylistTrackStartAudioTime = 0.0;
	float PlaylistTrackStartOffset = 0.f;

	// 지금 곡 길이(초). 모르면 0.
	float PlaylistTrackLength = 0.f;

	// 반복으로 설정된 곡은 끝났다는 알림이 오지 않는다. 곡 길이만큼 지나면 넘긴다.
	FTimerHandle PlaylistAdvanceTimer;

	// 반복 곡 경고를 한 번만 남긴다.
	bool bWarnedLoopingPlaylistTrack = false;

	// 플레이어 UI 에서 고르는 설정. LoadPlaylistPreferences 가 채운다.
	bool bPlaylistPaused = false;
	bool bPlaylistAutoAdvance = true;
	bool bPlaylistShuffle = false;

	// 이벤트별 마지막 재생 시각. 연타를 걸러내는 데 쓴다.
	TMap<EFF_AudioEvent, double> LastPlayTimeSeconds;

	int32 PlayedCues = 0;
	int32 MissingCues = 0;
	int32 SuppressedCues = 0;
	int32 StartedAmbienceTracks = 0;

	// 개발용 콘솔 명령. 이름 충돌·해제 규칙은 FFF_DebugConsoleCommands 가 맡는다.
	FFF_DebugConsoleCommands DebugCommands;
};
