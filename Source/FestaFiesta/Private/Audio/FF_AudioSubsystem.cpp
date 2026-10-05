// Copyright Epic Games, Inc. All Rights Reserved.

#include "Audio/FF_AudioSubsystem.h"

#include "Audio/FF_AudioBankData.h"
#include "Audio/FF_AudioSettings.h"
#include "Camera/FF_FestivalGameMode.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "TimerManager.h"
#include "UI/Settings/FF_RuntimeSettingsSubsystem.h"

DEFINE_LOG_CATEGORY(LogFestaFiestaAudio);

namespace
{
	// 크로스페이드가 끝난 뒤 이전 컴포넌트를 정리하기까지의 여유.
	// 페이드 시간에 딱 맞춰 지우면 마지막 순간이 잘려서 툭 끊긴다.
	constexpr float FadeCleanupPaddingSeconds = 0.25f;

	// 이어 틀 위치가 곡 끝에서 이만큼 안쪽이면 다음 곡으로 넘긴다.
	// 남은 몇 초만 틀고 바로 넘어가면 끊긴 것처럼 들린다.
	constexpr float PlaylistResumeTailSeconds = 8.f;

	// 이전 곡 버튼: 지금 곡을 이만큼 넘게 들었으면 지금 곡의 처음으로 간다.
	constexpr float PlaylistPreviousRestartSeconds = 3.f;

	// 플레이리스트 설정을 남기는 자리(GameUserSettings.ini).
	const TCHAR* const PlaylistConfigSection = TEXT("FestaFiesta.Playlist");
}

bool UFF_AudioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UFF_AudioSubsystem* UFF_AudioSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UFF_AudioSubsystem>() : nullptr;
}

void UFF_AudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RegisterDebugConsoleCommands();
	BindRuntimeSettings();
	LoadPlaylistPreferences();
}

void UFF_AudioSubsystem::Deinitialize()
{
	UnbindRuntimeSettings();
	UnregisterDebugConsoleCommands();
	StopAmbience();
	Super::Deinitialize();
}

const UFF_AudioBankData* UFF_AudioSubsystem::ResolveBank() const
{
	// 1. 코드에서 직접 꽂은 것.
	if (const UFF_AudioBankData* Direct = BankOverride.LoadSynchronous())
	{
		return Direct;
	}

	// 2. 이 레벨의 GameMode 지정. 맵마다 분위기가 다를 수 있다.
	const UWorld* World = GetWorld();
	if (const AFF_FestivalGameMode* GameMode =
		World ? Cast<AFF_FestivalGameMode>(World->GetAuthGameMode()) : nullptr)
	{
		if (const UFF_AudioBankData* FromLevel = GameMode->AudioBankOverride.LoadSynchronous())
		{
			return FromLevel;
		}
	}

	// 3. 프로젝트 기본값.
	if (const UFF_AudioSettings* Settings = UFF_AudioSettings::Get())
	{
		if (const UFF_AudioBankData* FromSettings = Settings->DefaultAudioBank.LoadSynchronous())
		{
			return FromSettings;
		}
	}

	return nullptr;
}

USoundBase* UFF_AudioSubsystem::PickSound(const FFF_AudioCue& Cue) const
{
	// 변형이 있으면 그중에서 고른다. 기본 소리도 후보에 넣어 골고루 나오게 한다.
	if (Cue.Variations.Num() > 0)
	{
		const int32 Total = Cue.Variations.Num() + (Cue.Sound.IsNull() ? 0 : 1);
		const int32 Index = FMath::RandRange(0, Total - 1);

		if (Index < Cue.Variations.Num())
		{
			return Cue.Variations[Index].LoadSynchronous();
		}
	}
	return Cue.Sound.LoadSynchronous();
}

bool UFF_AudioSubsystem::CanPlay(const EFF_AudioEvent Event, const FFF_AudioCue*& OutCue)
{
	OutCue = nullptr;

	if (!bAudioEnabled)
	{
		return false;
	}

	const UFF_AudioBankData* Bank = ResolveBank();
	if (!Bank)
	{
		++MissingCues;
		return false;
	}

	const FFF_AudioCue* Cue = Bank->FindCue(Event);
	if (!Cue || !Cue->IsValid())
	{
		// 뱅크에 없는 이벤트다. 아직 소리를 안 붙인 것이므로 경고까지 할 일은 아니다.
		++MissingCues;
		return false;
	}

	// 같은 소리가 겹쳐 울리면 찢어지는 소리가 난다.
	// 불꽃이 한 번에 여러 발 터지거나 NPC 여럿이 동시에 줄을 설 때가 그렇다.
	if (Cue->MinRetriggerSeconds > 0.f)
	{
		const UWorld* World = GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;

		if (const double* Last = LastPlayTimeSeconds.Find(Event))
		{
			if (Now - *Last < Cue->MinRetriggerSeconds)
			{
				++SuppressedCues;
				return false;
			}
		}
		LastPlayTimeSeconds.Add(Event, Now);
	}

	OutCue = Cue;
	return true;
}

bool UFF_AudioSubsystem::PlayEvent2D(const EFF_AudioEvent Event)
{
	const FFF_AudioCue* Cue = nullptr;
	if (!CanPlay(Event, Cue))
	{
		return false;
	}

	USoundBase* Sound = PickSound(*Cue);
	if (!Sound)
	{
		++MissingCues;
		return false;
	}

	UGameplayStatics::PlaySound2D(
		this, Sound,
		Cue->VolumeMultiplier * GetChannelVolume(ResolveChannel(Event, *Cue)),
		FMath::FRandRange(Cue->PitchMin, Cue->PitchMax),
		0.f, nullptr, nullptr, true);

	++PlayedCues;
	return true;
}

bool UFF_AudioSubsystem::PlayEventAtLocation(const EFF_AudioEvent Event, const FVector& WorldLocation)
{
	const FFF_AudioCue* Cue = nullptr;
	if (!CanPlay(Event, Cue))
	{
		return false;
	}

	USoundBase* Sound = PickSound(*Cue);
	if (!Sound)
	{
		++MissingCues;
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UGameplayStatics::PlaySoundAtLocation(
		World, Sound, WorldLocation, FRotator::ZeroRotator,
		Cue->VolumeMultiplier * GetChannelVolume(ResolveChannel(Event, *Cue)),
		FMath::FRandRange(Cue->PitchMin, Cue->PitchMax),
		0.f, nullptr,
		Cue->Concurrency.LoadSynchronous());

	++PlayedCues;
	return true;
}

void UFF_AudioSubsystem::SetAmbience(const EFF_AudioAmbience Ambience)
{
	// 같은 것을 다시 요청하면 무시한다.
	//
	// 이게 없으면 매 틱 상태를 확인해서 호출하는 코드가 붙었을 때 소리가 계속 처음부터
	// 다시 재생된다. 호출부가 조심하게 만드는 것보다 여기서 막는 편이 안전하다.
	// 플레이리스트도 같다. 단계가 바뀌어 다시 요청돼도 듣던 곡을 끊지 않는다.
	if (CurrentAmbience == Ambience)
	{
		return;
	}

	if (!bAudioEnabled)
	{
		CurrentAmbience = Ambience;
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UFF_AudioBankData* Bank = ResolveBank();

	/* 이름만 다르고 같은 곡이면 끊지 않고 이름만 바꾼다.

	   뱅크에서 피날레 곡과 최고 등급 전용곡에 같은 곡을 넣어 두었더니, 둘 사이를 오갈 때마다
	   곡이 처음부터 다시 시작해 끊겼다(2026-10-05 피드백). 플레이리스트는 곡 목록이라 여기서 다루지 않는다.
	*/
	if (Bank && Ambience != EFF_AudioAmbience::Playlist && CurrentAmbience != EFF_AudioAmbience::Playlist)
	{
		const FFF_AudioAmbienceTrack* Playing = FindActiveTrack(Bank);
		const FFF_AudioAmbienceTrack* Next = Bank->FindAmbience(Ambience);
		if (Playing && Next && Playing->IsValid() && Next->Sound == Playing->Sound)
		{
			CurrentAmbience = Ambience;
			return;
		}
	}

	// 플레이리스트를 떠나면 들은 자리를 남긴다. 돌아오면 거기서 잇는다.
	// 멈춰 둔 상태는 풀어 둔다. 다음에 돌아왔을 때 소리가 안 나면 고장으로 보인다.
	const bool bLeavingPlaylist = CurrentAmbience == EFF_AudioAmbience::Playlist;
	if (bLeavingPlaylist)
	{
		CapturePlaylistPosition();
		bPlaylistPaused = false;
		World->GetTimerManager().ClearTimer(PlaylistAdvanceTimer);
	}

	// 이전 곡을 페이드아웃시킨다. 즉시 끊으면 툭 하고 잘린다.
	const FFF_AudioAmbienceTrack* Previous = FindActiveTrack(Bank);
	RetireAmbienceComponent(Previous ? FMath::Max(0.f, Previous->FadeOutSeconds) : 1.f);

	CurrentAmbience = Ambience;

	if (Ambience == EFF_AudioAmbience::Playlist)
	{
		// 곡을 틀면 StartPlaylistTrack 이 알린다. 못 틀었어도 플레이리스트가 켜진 것은 알린다.
		if (!StartPlaylistTrack())
		{
			OnPlaylistChanged.Broadcast();
		}
		return;
	}

	if (bLeavingPlaylist)
	{
		OnPlaylistChanged.Broadcast();
	}

	const FFF_AudioAmbienceTrack* Track = Bank ? Bank->FindAmbience(Ambience) : nullptr;
	if (!Track || !Track->IsValid())
	{
		// 이 상황에 붙은 곡이 없다. 이전 곡만 끄고 끝낸다.
		return;
	}

	if (USoundBase* Sound = Track->Sound.LoadSynchronous())
	{
		SpawnAmbienceComponent(Sound, *Track, 0.f);
	}
}

void UFF_AudioSubsystem::RetireAmbienceComponent(const float FadeOutSeconds)
{
	UWorld* World = GetWorld();
	if (!AmbienceComponent || !World)
	{
		return;
	}

	// 페이드 중이던 게 또 있으면 그건 지금 정리한다. 두 개까지만 겹치게 둔다.
	if (FadingAmbienceComponent)
	{
		FadingAmbienceComponent->Stop();
		FadingAmbienceComponent = nullptr;
	}

	// 멈춰 둔 곡은 풀어야 페이드아웃이 진행된다. 멈춘 채로 두면 페이드 칸에 소리 없이 남는다.
	AmbienceComponent->SetPaused(false);
	AmbienceComponent->FadeOut(FadeOutSeconds, 0.f);
	FadingAmbienceComponent = AmbienceComponent;
	AmbienceComponent = nullptr;

	// 페이드가 끝나면 참조를 놓는다. 안 그러면 컴포넌트가 계속 살아 있다.
	World->GetTimerManager().SetTimer(
		FadingCleanupTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			FadingAmbienceComponent = nullptr;
		}),
		FadeOutSeconds + FadeCleanupPaddingSeconds, false);
}

void UFF_AudioSubsystem::SpawnAmbienceComponent(
	USoundBase* Sound, const FFF_AudioAmbienceTrack& Track, const float StartSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !Sound)
	{
		return;
	}

	/* 곡 볼륨과 배경음 비율은 컴포넌트 볼륨에 두고, 페이드는 1 까지만 올린다.

	   예전에는 컴포넌트 볼륨과 페이드 목표에 곡 볼륨을 둘 다 넣어서 곡 볼륨이 제곱으로 먹었다.
	   0.5 로 두면 0.25 로 들렸다. 볼륨을 한 곳에 모아야 설정이 바뀌었을 때 그 값만 다시 걸 수 있다.
	*/
	AmbienceTrackVolume = Track.VolumeMultiplier;
	++StartedAmbienceTracks;
	AmbienceComponent = UGameplayStatics::SpawnSound2D(
		World, Sound, AmbienceTrackVolume * GetChannelVolume(EFF_AudioChannel::Music),
		1.f, StartSeconds, nullptr, true, false);

	if (AmbienceComponent)
	{
		// FadeIn 은 재생을 다시 건다. 시작 위치를 같이 넘겨야 이어 틀기가 처음으로 돌아가지 않는다.
		AmbienceComponent->FadeIn(FMath::Max(0.f, Track.FadeInSeconds), 1.f, StartSeconds);
		AmbienceComponent->OnAudioFinishedNative.AddUObject(this, &UFF_AudioSubsystem::HandleAmbienceFinished);
	}
}

const FFF_AudioAmbienceTrack* UFF_AudioSubsystem::FindActiveTrack(const UFF_AudioBankData* Bank) const
{
	if (!Bank)
	{
		return nullptr;
	}

	if (CurrentAmbience == EFF_AudioAmbience::Playlist)
	{
		const int32 TrackIndex = PlaylistOrder.IsValidIndex(PlaylistCursor) ? PlaylistOrder[PlaylistCursor] : INDEX_NONE;
		return Bank->FestivalPlaylist.IsValidIndex(TrackIndex) ? &Bank->FestivalPlaylist[TrackIndex] : nullptr;
	}

	return Bank->FindAmbience(CurrentAmbience);
}

bool UFF_AudioSubsystem::HasPlaylist() const
{
	const UFF_AudioBankData* Bank = ResolveBank();
	if (!Bank)
	{
		return false;
	}

	for (const FFF_AudioAmbienceTrack& Track : Bank->FestivalPlaylist)
	{
		if (Track.IsValid())
		{
			return true;
		}
	}
	return false;
}

void UFF_AudioSubsystem::SkipPlaylistTrack()
{
	if (CurrentAmbience == EFF_AudioAmbience::Playlist && bAudioEnabled)
	{
		AdvancePlaylist();
	}
}

void UFF_AudioSubsystem::RebuildPlaylistOrder(const int32 TrackCount, const bool bShuffle, const int32 AvoidFirstTrack)
{
	PlaylistOrder.Reset(TrackCount);
	for (int32 Index = 0; Index < TrackCount; ++Index)
	{
		PlaylistOrder.Add(Index);
	}

	if (!bShuffle || TrackCount < 2)
	{
		return;
	}

	for (int32 Index = TrackCount - 1; Index > 0; --Index)
	{
		PlaylistOrder.Swap(Index, FMath::RandRange(0, Index));
	}

	// 바퀴가 바뀌는 자리에서 같은 곡이 연달아 나오지 않게 한다.
	if (PlaylistOrder[0] == AvoidFirstTrack)
	{
		PlaylistOrder.Swap(0, FMath::RandRange(1, TrackCount - 1));
	}
}

bool UFF_AudioSubsystem::StartPlaylistTrack()
{
	UWorld* World = GetWorld();
	const UFF_AudioBankData* Bank = ResolveBank();
	const int32 Count = Bank ? Bank->FestivalPlaylist.Num() : 0;
	if (!World || Count == 0)
	{
		UE_LOG(LogFestaFiestaAudio, Warning,
			TEXT("[사운드] 플레이리스트가 비어 있다. 오디오 뱅크의 Festival Playlist 를 채울 것."));
		return false;
	}

	// 뱅크 목록이 바뀌었으면(에디터에서 곡을 넣고 뺐으면) 순서를 새로 만든다.
	if (PlaylistOrder.Num() != Count)
	{
		RebuildPlaylistOrder(Count, bPlaylistShuffle, INDEX_NONE);
		PlaylistCursor = 0;
		PlaylistResumeSeconds = 0.f;
	}

	for (int32 Tries = 0; Tries < Count; ++Tries)
	{
		if (!PlaylistOrder.IsValidIndex(PlaylistCursor))
		{
			PlaylistCursor = 0;
		}

		const FFF_AudioAmbienceTrack& Track = Bank->FestivalPlaylist[PlaylistOrder[PlaylistCursor]];
		USoundBase* Sound = Track.IsValid() ? Track.Sound.LoadSynchronous() : nullptr;
		if (!Sound)
		{
			// 빈 칸이다. 다음 곡으로.
			++PlaylistCursor;
			PlaylistResumeSeconds = 0.f;
			continue;
		}

		// 반복 곡은 GetDuration 이 "무한" 을 돌려준다. 파형이면 실제 길이를 읽는다.
		const USoundWave* Wave = Cast<USoundWave>(Sound);
		const bool bLooping = Sound->IsLooping();
		PlaylistTrackLength = Wave ? Wave->Duration : (bLooping ? 0.f : Sound->GetDuration());

		if (PlaylistTrackLength > 0.f
			&& PlaylistResumeSeconds > PlaylistTrackLength - PlaylistResumeTailSeconds)
		{
			++PlaylistCursor;
			PlaylistResumeSeconds = 0.f;
			continue;
		}

		const float StartSeconds = FMath::Max(0.f, PlaylistResumeSeconds);
		SpawnAmbienceComponent(Sound, Track, StartSeconds);
		PlaylistTrackStartAudioTime = World->GetAudioTimeSeconds();
		PlaylistTrackStartOffset = StartSeconds;

		// 반복 곡은 끝났다는 알림이 오지 않는다. 길이만큼 지나면 넘긴다.
		World->GetTimerManager().ClearTimer(PlaylistAdvanceTimer);
		if (bLooping)
		{
			if (!bWarnedLoopingPlaylistTrack)
			{
				bWarnedLoopingPlaylistTrack = true;
				UE_LOG(LogFestaFiestaAudio, Warning,
					TEXT("[사운드] 플레이리스트 곡 %s 이 반복 재생으로 되어 있다. 사운드 애셋의 Looping 을 끌 것."),
					*Sound->GetName());
			}
			if (PlaylistTrackLength > 0.f)
			{
				World->GetTimerManager().SetTimer(
					PlaylistAdvanceTimer,
					FTimerDelegate::CreateWeakLambda(this, [this]() { HandlePlaylistTrackEnded(); }),
					FMath::Max(0.1f, PlaylistTrackLength - StartSeconds), false);
			}
		}

		bPlaylistPaused = false;

		UE_LOG(LogFestaFiestaAudio, Log,
			TEXT("[사운드] 플레이리스트 %d/%d: %s (%.1f초부터)"),
			PlaylistCursor + 1, Count, *Sound->GetName(), StartSeconds);
		OnPlaylistChanged.Broadcast();
		return true;
	}

	UE_LOG(LogFestaFiestaAudio, Warning,
		TEXT("[사운드] 플레이리스트에 틀 수 있는 곡이 없다. 오디오 뱅크의 Festival Playlist 를 확인할 것."));
	return false;
}

void UFF_AudioSubsystem::AdvancePlaylist()
{
	if (CurrentAmbience != EFF_AudioAmbience::Playlist || !bAudioEnabled)
	{
		return;
	}

	const int32 FinishingTrack = GetCurrentPlaylistTrack();
	const int32 NextCursor = PlaylistCursor + 1;

	// 한 바퀴 돌았다. 셔플이면 새로 섞는다. 바뀌는 자리에서 같은 곡이 연달아 나오지 않게 한다.
	if (NextCursor >= PlaylistOrder.Num())
	{
		RestartPlaylistAt(0, bPlaylistShuffle, FinishingTrack);
		return;
	}

	RestartPlaylistAt(NextCursor);
}

void UFF_AudioSubsystem::HandlePlaylistTrackEnded()
{
	if (bPlaylistAutoAdvance)
	{
		AdvancePlaylist();
		return;
	}

	// 자동 다음 곡을 껐다. 같은 곡을 처음부터.
	if (CurrentAmbience == EFF_AudioAmbience::Playlist && bAudioEnabled)
	{
		RestartPlaylistAt(PlaylistCursor);
	}
}

void UFF_AudioSubsystem::RestartPlaylistAt(const int32 Cursor, const bool bReshuffle, const int32 AvoidFirstTrack)
{
	if (CurrentAmbience != EFF_AudioAmbience::Playlist || !bAudioEnabled)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlaylistAdvanceTimer);
	}

	// 지금 곡을 페이드아웃시킨다. 저절로 끝난 곡은 이미 비워져 있다(HandleAmbienceFinished).
	const UFF_AudioBankData* Bank = ResolveBank();
	const FFF_AudioAmbienceTrack* Leaving = FindActiveTrack(Bank);
	RetireAmbienceComponent(Leaving ? FMath::Max(0.f, Leaving->FadeOutSeconds) : 1.f);

	if (bReshuffle && Bank)
	{
		RebuildPlaylistOrder(Bank->FestivalPlaylist.Num(), true, AvoidFirstTrack);
	}

	PlaylistCursor = Cursor;
	PlaylistResumeSeconds = 0.f;
	bPlaylistPaused = false;
	StartPlaylistTrack();
}

int32 UFF_AudioSubsystem::GetPlaylistTrackCount() const
{
	const UFF_AudioBankData* Bank = ResolveBank();
	return Bank ? Bank->FestivalPlaylist.Num() : 0;
}

FText UFF_AudioSubsystem::GetPlaylistTrackDisplayName(const int32 TrackIndex) const
{
	const UFF_AudioBankData* Bank = ResolveBank();
	if (!Bank || !Bank->FestivalPlaylist.IsValidIndex(TrackIndex))
	{
		return FText::GetEmpty();
	}

	const FFF_AudioAmbienceTrack& Track = Bank->FestivalPlaylist[TrackIndex];
	if (!Track.DisplayName.IsEmpty())
	{
		return Track.DisplayName;
	}

	// 이름을 안 넣었으면 애셋 이름으로 대신한다. 불러오지 않고 경로에서 읽는다.
	FString Name = Track.Sound.IsNull() ? FString(TEXT("(빈 칸)")) : Track.Sound.GetAssetName();
	Name.ReplaceInline(TEXT("_"), TEXT(" "));
	return FText::FromString(Name);
}

int32 UFF_AudioSubsystem::GetCurrentPlaylistTrack() const
{
	return PlaylistOrder.IsValidIndex(PlaylistCursor) ? PlaylistOrder[PlaylistCursor] : INDEX_NONE;
}

void UFF_AudioSubsystem::PlayPlaylistTrack(const int32 TrackIndex)
{
	if (CurrentAmbience != EFF_AudioAmbience::Playlist || !bAudioEnabled)
	{
		return;
	}

	const int32 Count = GetPlaylistTrackCount();
	if (TrackIndex < 0 || TrackIndex >= Count)
	{
		return;
	}

	if (PlaylistOrder.Num() != Count)
	{
		RebuildPlaylistOrder(Count, bPlaylistShuffle, INDEX_NONE);
	}

	const int32 Cursor = PlaylistOrder.IndexOfByKey(TrackIndex);
	if (Cursor != INDEX_NONE)
	{
		RestartPlaylistAt(Cursor);
	}
}

void UFF_AudioSubsystem::PreviousPlaylistTrack()
{
	if (CurrentAmbience != EFF_AudioAmbience::Playlist || !bAudioEnabled || PlaylistOrder.Num() == 0)
	{
		return;
	}

	if (GetPlaylistPlaybackSeconds() > PlaylistPreviousRestartSeconds)
	{
		RestartPlaylistAt(PlaylistCursor);
		return;
	}

	const int32 Count = PlaylistOrder.Num();
	RestartPlaylistAt((PlaylistCursor - 1 + Count) % Count);
}

void UFF_AudioSubsystem::SetPlaylistPaused(const bool bPaused)
{
	UWorld* World = GetWorld();
	if (CurrentAmbience != EFF_AudioAmbience::Playlist || !World || bPaused == bPlaylistPaused)
	{
		return;
	}

	if (!AmbienceComponent)
	{
		// 흐르는 곡이 없다(틀 곡이 없었거나 넘어가는 중). 잇기를 누르면 지금 자리부터 다시 튼다.
		if (!bPaused)
		{
			bPlaylistPaused = false;
			StartPlaylistTrack();
		}
		return;
	}

	if (bPaused)
	{
		// 위치를 먼저 남긴다. 멈춘 동안에는 남긴 값이 곧 위치다(CapturePlaylistPosition).
		CapturePlaylistPosition();
		bPlaylistPaused = true;
		AmbienceComponent->SetPaused(true);
		World->GetTimerManager().PauseTimer(PlaylistAdvanceTimer);
	}
	else
	{
		bPlaylistPaused = false;
		AmbienceComponent->SetPaused(false);
		PlaylistTrackStartAudioTime = World->GetAudioTimeSeconds();
		PlaylistTrackStartOffset = PlaylistResumeSeconds;
		World->GetTimerManager().UnPauseTimer(PlaylistAdvanceTimer);
	}

	OnPlaylistChanged.Broadcast();
}

void UFF_AudioSubsystem::SetPlaylistAutoAdvance(const bool bAutoAdvance)
{
	if (bPlaylistAutoAdvance == bAutoAdvance)
	{
		return;
	}
	bPlaylistAutoAdvance = bAutoAdvance;
	SavePlaylistPreferences();
	OnPlaylistChanged.Broadcast();
}

void UFF_AudioSubsystem::SetPlaylistShuffle(const bool bShuffle)
{
	if (bPlaylistShuffle == bShuffle)
	{
		return;
	}
	bPlaylistShuffle = bShuffle;
	SavePlaylistPreferences();

	/* 순서만 다시 만든다. 지금 곡은 끊지 않는다.

	   섞을 때는 지금 곡을 맨 앞에 둬서 남은 곡이 전부 뒤에 무작위로 오게 한다.
	   풀 때는 뱅크 순서로 돌아가 지금 곡 다음부터 차례로 흐른다.
	*/
	const int32 Count = GetPlaylistTrackCount();
	if (PlaylistOrder.Num() == Count && Count > 0)
	{
		const int32 Current = GetCurrentPlaylistTrack();
		RebuildPlaylistOrder(Count, bShuffle, INDEX_NONE);
		const int32 Found = Current == INDEX_NONE ? INDEX_NONE : PlaylistOrder.IndexOfByKey(Current);
		if (Found == INDEX_NONE)
		{
			PlaylistCursor = 0;
		}
		else if (bShuffle)
		{
			PlaylistOrder.Swap(0, Found);
			PlaylistCursor = 0;
		}
		else
		{
			PlaylistCursor = Found;
		}
	}

	OnPlaylistChanged.Broadcast();
}

float UFF_AudioSubsystem::GetPlaylistPlaybackSeconds() const
{
	const UWorld* World = GetWorld();
	if (CurrentAmbience != EFF_AudioAmbience::Playlist || bPlaylistPaused || !AmbienceComponent || !World)
	{
		return PlaylistResumeSeconds;
	}

	const float Position = PlaylistTrackStartOffset
		+ static_cast<float>(World->GetAudioTimeSeconds() - PlaylistTrackStartAudioTime);
	return PlaylistTrackLength > 0.f
		? FMath::Clamp(Position, 0.f, PlaylistTrackLength)
		: FMath::Max(0.f, Position);
}

void UFF_AudioSubsystem::LoadPlaylistPreferences()
{
	// 처음이면(저장된 값이 없으면) 자동 다음 곡은 켜고, 셔플은 뱅크 기본값을 따른다.
	bool bSavedAutoAdvance = true;
	if (GConfig && GConfig->GetBool(PlaylistConfigSection, TEXT("AutoAdvance"), bSavedAutoAdvance, GGameUserSettingsIni))
	{
		bPlaylistAutoAdvance = bSavedAutoAdvance;
	}

	bool bSavedShuffle = false;
	if (GConfig && GConfig->GetBool(PlaylistConfigSection, TEXT("Shuffle"), bSavedShuffle, GGameUserSettingsIni))
	{
		bPlaylistShuffle = bSavedShuffle;
	}
	else if (const UFF_AudioBankData* Bank = ResolveBank())
	{
		bPlaylistShuffle = Bank->bShufflePlaylist;
	}
}

void UFF_AudioSubsystem::SavePlaylistPreferences() const
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetBool(PlaylistConfigSection, TEXT("AutoAdvance"), bPlaylistAutoAdvance, GGameUserSettingsIni);
	GConfig->SetBool(PlaylistConfigSection, TEXT("Shuffle"), bPlaylistShuffle, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UFF_AudioSubsystem::CapturePlaylistPosition()
{
	// 멈춰 둔 동안에는 멈출 때 남긴 값이 곧 위치다. 다시 계산하면 멈춘 시간만큼 앞서 간다.
	if (bPlaylistPaused)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || !AmbienceComponent)
	{
		// 지금 곡이 없다(틀지 못했거나 이미 끝났다). 남긴 값을 그대로 둔다.
		return;
	}

	float Position = PlaylistTrackStartOffset
		+ static_cast<float>(World->GetAudioTimeSeconds() - PlaylistTrackStartAudioTime);
	if (PlaylistTrackLength > 0.f)
	{
		Position = FMath::Clamp(Position, 0.f, PlaylistTrackLength);
	}
	PlaylistResumeSeconds = FMath::Max(0.f, Position);
}

void UFF_AudioSubsystem::HandleAmbienceFinished(UAudioComponent* Component)
{
	/* 지금 곡이 끝난 것만 다시 튼다.

	   곡을 갈아탈 때 이전 곡은 페이드아웃 칸으로 옮겨지고, 끌 때는 참조를 먼저 비운다.
	   그 곡들도 끝났다고 알려오는데, 다시 틀면 꺼진 곡이 되살아난다.
	*/
	if (!Component || Component != AmbienceComponent || !bAudioEnabled)
	{
		return;
	}

	// 플레이리스트는 다음 곡(또는 같은 곡)으로 간다. 끝난 곡은 페이드아웃할 것이 없으니 비우고 넘긴다.
	if (CurrentAmbience == EFF_AudioAmbience::Playlist)
	{
		AmbienceComponent = nullptr;
		HandlePlaylistTrackEnded();
		return;
	}

	// 최고 등급 전용곡도 연출이 끝날 때까지 되풀이한다. 곡이 연출보다 짧아도 끝까지 조용해지지 않는다.
	// 연출이 끝나면 오디오 결선이 다음 곡으로 갈아탄다(UFF_AudioEventLink::HandleFinaleShowStateChanged).
	Component->Play(0.f);
}

bool UFF_AudioSubsystem::HasAmbienceTrack(const EFF_AudioAmbience Ambience) const
{
	const UFF_AudioBankData* Bank = ResolveBank();
	const FFF_AudioAmbienceTrack* Track = Bank ? Bank->FindAmbience(Ambience) : nullptr;
	return Track && Track->IsValid();
}

float UFF_AudioSubsystem::GetChannelVolume(const EFF_AudioChannel Channel) const
{
	// 설정 시스템이 없는 맵(테스트 등)에서는 비율을 걸지 않는다. 소리가 안 나는 것보다 낫다.
	const UFF_RuntimeSettingsSubsystem* Runtime = UFF_RuntimeSettingsSubsystem::Get(this);
	if (!Runtime)
	{
		return 1.f;
	}

	const FFF_AudioVolumes Volumes = Runtime->GetAudioVolumes();
	switch (Channel)
	{
	case EFF_AudioChannel::Music:     return Volumes.Music;
	case EFF_AudioChannel::Fireworks: return Volumes.Fireworks;
	case EFF_AudioChannel::Ui:        return Volumes.Ui;
	default:                          return Volumes.Effects;
	}
}

EFF_AudioChannel UFF_AudioSubsystem::ResolveChannel(const EFF_AudioEvent Event, const FFF_AudioCue& Cue)
{
	if (Cue.Channel != EFF_AudioChannel::Effects)
	{
		return Cue.Channel;
	}

	switch (Event)
	{
	case EFF_AudioEvent::FinaleBurst:
	case EFF_AudioEvent::FinaleLaunch:
	case EFF_AudioEvent::FinaleShowStarted:
	case EFF_AudioEvent::FinaleShowEnded:
		return EFF_AudioChannel::Fireworks;

	case EFF_AudioEvent::UiClick:
	case EFF_AudioEvent::UiHover:
	case EFF_AudioEvent::UiConfirm:
	case EFF_AudioEvent::UiCancel:
	case EFF_AudioEvent::UiError:
		return EFF_AudioChannel::Ui;

	default:
		return EFF_AudioChannel::Effects;
	}
}

void UFF_AudioSubsystem::BindRuntimeSettings()
{
	if (UFF_RuntimeSettingsSubsystem* Runtime = UFF_RuntimeSettingsSubsystem::Get(this))
	{
		Runtime->OnSettingsChanged.AddUniqueDynamic(this, &UFF_AudioSubsystem::HandleRuntimeSettingsChanged);
	}
}

void UFF_AudioSubsystem::UnbindRuntimeSettings()
{
	// 설정 시스템은 게임 인스턴스라 월드보다 오래 산다. 안 끊으면 사라진 월드의 이 객체를 계속 부른다.
	if (UFF_RuntimeSettingsSubsystem* Runtime = UFF_RuntimeSettingsSubsystem::Get(this))
	{
		Runtime->OnSettingsChanged.RemoveDynamic(this, &UFF_AudioSubsystem::HandleRuntimeSettingsChanged);
	}
}

void UFF_AudioSubsystem::HandleRuntimeSettingsChanged()
{
	if (AmbienceComponent)
	{
		AmbienceComponent->SetVolumeMultiplier(AmbienceTrackVolume * GetChannelVolume(EFF_AudioChannel::Music));
	}
}

void UFF_AudioSubsystem::StopAmbience()
{
	// 플레이리스트를 끄면 들은 자리를 남긴다. 소리를 다시 켜면 거기서 잇는다(SetAudioEnabled).
	const bool bWasPlaylist = CurrentAmbience == EFF_AudioAmbience::Playlist;
	if (bWasPlaylist)
	{
		CapturePlaylistPosition();
		bPlaylistPaused = false;
	}

	if (AmbienceComponent)
	{
		// 참조를 먼저 비운다. Stop 이 곧바로 "끝났다" 를 알리는데, 그때 지금 곡으로 보이면 다시 튼다.
		UAudioComponent* Stopping = AmbienceComponent;
		AmbienceComponent = nullptr;
		Stopping->Stop();
	}
	if (FadingAmbienceComponent)
	{
		FadingAmbienceComponent->Stop();
		FadingAmbienceComponent = nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FadingCleanupTimer);
		World->GetTimerManager().ClearTimer(PlaylistAdvanceTimer);
	}

	CurrentAmbience = EFF_AudioAmbience::None;

	if (bWasPlaylist)
	{
		OnPlaylistChanged.Broadcast();
	}
}

void UFF_AudioSubsystem::SetAudioEnabled(const bool bEnabled)
{
	bAudioEnabled = bEnabled;

	if (!bEnabled)
	{
		// 배경음은 끊되 현재 상황은 기억해둔다. 다시 켰을 때 이어갈 수 있게.
		const EFF_AudioAmbience Remembered = CurrentAmbience;
		StopAmbience();
		CurrentAmbience = Remembered;
		return;
	}

	// 다시 켜면 기억해둔 배경음을 되살린다.
	const EFF_AudioAmbience Resume = CurrentAmbience;
	CurrentAmbience = EFF_AudioAmbience::None;
	SetAmbience(Resume);
}

void UFF_AudioSubsystem::RegisterDebugConsoleCommands()
{
	// 콘솔 이름은 프로세스 전역인데 이 서브시스템은 월드마다 생긴다.
	// 이름 충돌·해제 규칙은 FFF_DebugConsoleCommands 가 맡는다.
	DebugCommands.Add(
		TEXT("FF.Audio.Stat"),
		TEXT("사운드 재생 상태와 사용 중인 뱅크를 출력한다."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			const UFF_AudioBankData* Bank = ResolveBank();
			UE_LOG(LogFestaFiestaAudio, Display,
				TEXT("[사운드] 켜짐=%s 재생=%d 누락=%d 억제=%d 배경음=%d"),
				bAudioEnabled ? TEXT("예") : TEXT("아니오"),
				PlayedCues, MissingCues, SuppressedCues, static_cast<int32>(CurrentAmbience));
			UE_LOG(LogFestaFiestaAudio, Display,
				TEXT("  뱅크: %s / 등록된 효과음 %d개 / 배경음 %d개"),
				Bank ? *Bank->GetName() : TEXT("없음"),
				Bank ? Bank->Cues.Num() : 0,
				Bank ? Bank->Ambiences.Num() : 0);
			UE_LOG(LogFestaFiestaAudio, Display,
				TEXT("  플레이리스트: %d곡 / 지금 %d번째 / 이어 틀 위치 %.1f초"),
				Bank ? Bank->FestivalPlaylist.Num() : 0, PlaylistCursor + 1, PlaylistResumeSeconds);
		}));

	DebugCommands.Add(
		TEXT("FF.Audio.Play"),
		TEXT("이벤트 번호로 소리를 재생한다. 사용법: FF.Audio.Play 10"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogFestaFiestaAudio, Warning,
					TEXT("[사운드] 이벤트 번호가 필요하다. EFF_AudioEvent 값을 넣을 것."));
				return;
			}
			const int32 Value = FCString::Atoi(*Args[0]);
			const bool bPlayed = PlayEvent2D(static_cast<EFF_AudioEvent>(Value));
			UE_LOG(LogFestaFiestaAudio, Display,
				TEXT("[사운드] 이벤트 %d 재생 %s"), Value, bPlayed ? TEXT("성공") : TEXT("실패"));
		}));

	DebugCommands.Add(
		TEXT("FF.Audio.Next"),
		TEXT("플레이리스트를 다음 곡으로 넘긴다."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			if (CurrentAmbience != EFF_AudioAmbience::Playlist)
			{
				UE_LOG(LogFestaFiestaAudio, Display, TEXT("[사운드] 플레이리스트가 흐르는 중이 아니다."));
				return;
			}
			SkipPlaylistTrack();
		}));

	DebugCommands.Add(
		TEXT("FF.Audio.Mute"),
		TEXT("소리를 끄거나 켠다. 사용법: FF.Audio.Mute 1"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const bool bMute = Args.Num() == 0 || FCString::Atoi(*Args[0]) != 0;
			SetAudioEnabled(!bMute);
			UE_LOG(LogFestaFiestaAudio, Display,
				TEXT("[사운드] %s"), bMute ? TEXT("음소거") : TEXT("켜짐"));
		}));
}

void UFF_AudioSubsystem::UnregisterDebugConsoleCommands()
{
	// 내가 건 것만 지운다. 규칙은 FFF_DebugConsoleCommands 주석 참고.
	DebugCommands.UnregisterAll();
}
