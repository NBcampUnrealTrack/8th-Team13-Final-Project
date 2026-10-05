// Copyright Epic Games, Inc. All Rights Reserved.

#include "Audio/FF_AudioEventLink.h"

#include "Audio/FF_AudioSettings.h"
#include "Audio/FF_AudioSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Building/FF_BuildModeSubsystem.h"
#include "FF_FFZSWorldSubsystem.h"
#include "Festival/FF_FestivalProgressionSubsystem.h"
#include "Finale/FF_FinaleShowSubsystem.h"
#include "Queue/FF_QueueSubsystem.h"
#include "Time/FF_FestivalStageSubsystem.h"
#include "UI/Core/FF_UIFlowSettings.h"
#include "UI/Core/FF_UIFlowSubsystem.h"
#include "UI/Core/FF_UIFlowTypes.h"

bool UFF_AudioEventLink::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFF_AudioEventLink::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	/* 로비인지는 레벨로 가른다.

	   축제 단계는 게임 인스턴스가 들고 있어서 로비에 와도 값이 남는다. 처음 켜면 기본값인
	   준비 단계, 축제를 끝내고 돌아오면 결산 단계라 단계만 보면 로비에서 개장 전 곡이나
	   폐장 곡이 흘렀다. UI 흐름이 로비로 쓰는 레벨 설정을 그대로 읽어 같은 기준을 쓴다.
	*/
	bIsLobbyWorld = false;
	if (const UFF_UIFlowSettings* FlowSettings = UFF_UIFlowSettings::Get())
	{
		if (!FlowSettings->FrontendLevel.IsNull())
		{
			// PIE 에서는 패키지 이름에 UEDPIE_0_ 접두어가 붙어 떼고 비교한다.
			const FString WorldPackage = UWorld::RemovePIEPrefix(InWorld.GetOutermost()->GetName());
			bIsLobbyWorld = WorldPackage == FlowSettings->FrontendLevel.ToSoftObjectPath().GetLongPackageName();
		}
	}

	// Initialize 가 아니라 여기서 묶는다. 서브시스템 생성 순서는 보장되지 않아서
	// Initialize 시점엔 상대가 아직 없을 수 있다.
	BindAll();

	// 로비는 화면(타이틀/허브·지도)에 따라 곡이 갈린다. 화면 전환 신호가 없어 주기적으로 본다.
	if (bIsLobbyWorld)
	{
		InWorld.GetTimerManager().SetTimer(
			FrontendScreenPollTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]() { PollFrontendScreen(); }),
			0.2f, true, 0.f);
	}
	else
	{
		// 축제 레벨이면 플레이리스트 플레이어를 띄운다.
		InWorld.GetTimerManager().SetTimer(
			PlaylistPlayerCreateTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]() { TryCreatePlaylistPlayer(); }),
			0.5f, true, 0.f);
	}
}

void UFF_AudioEventLink::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FrontendScreenPollTimer);
		World->GetTimerManager().ClearTimer(PlaylistPlayerCreateTimer);
	}
	if (PlaylistPlayerWidget)
	{
		PlaylistPlayerWidget->RemoveFromParent();
		PlaylistPlayerWidget = nullptr;
	}
	UnbindAll();
	Super::Deinitialize();
}

UFF_AudioSubsystem* UFF_AudioEventLink::GetAudio() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UFF_AudioSubsystem>() : nullptr;
}

void UFF_AudioEventLink::BindAll()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!bBoundFinale)
	{
		if (UFF_FinaleShowSubsystem* Finale = World->GetSubsystem<UFF_FinaleShowSubsystem>())
		{
			Finale->OnBurst.AddDynamic(this, &UFF_AudioEventLink::HandleFinaleBurst);
			Finale->OnShowStateChanged.AddDynamic(this, &UFF_AudioEventLink::HandleFinaleShowStateChanged);
			Finale->OnLaunch.AddDynamic(this, &UFF_AudioEventLink::HandleFinaleLaunch);
			bBoundFinale = true;
		}
	}

	if (!bBoundStage)
	{
		// 단계는 게임 인스턴스가 소유한다. 월드가 바뀌어도 축제는 이어지기 때문이다.
		if (UFF_FestivalStageSubsystem* Stage =
			UGameInstance::GetSubsystem<UFF_FestivalStageSubsystem>(World->GetGameInstance()))
		{
			Stage->OnStageChanged.AddDynamic(this, &UFF_AudioEventLink::HandleStageChanged);
			Stage->OnEnvironmentStateChanged.AddDynamic(this, &UFF_AudioEventLink::HandleEnvironmentChanged);
			bBoundStage = true;

			// 신호는 바뀔 때만 온다. 지금 상태를 한 번 읽어서 배경음을 맞춘다.
			// 밤낮도 같이 읽는다. 빠뜨리면 밤에 레벨이 열렸을 때 다음 밤 신호가 올 때까지
			// 낮 곡이 돈다 — 그 신호는 하루에 한 번뿐이다.
			LastStage = Stage->CurrentStage;
			bLastIsNight = Stage->GetIsNightActive();
			RefreshAmbience();
		}
	}

	if (!bBoundBuild)
	{
		if (UFF_BuildModeSubsystem* Build = World->GetSubsystem<UFF_BuildModeSubsystem>())
		{
			Build->OnBuildModeStateChanged.AddDynamic(this, &UFF_AudioEventLink::HandleBuildModeStateChanged);
			bBoundBuild = true;
		}
	}

	if (!bBoundZoneWorld)
	{
		// 시설 설치·철거는 Zone 이 확정한 뒤에만 방송된다.
		// 건축 모드 상태만 보면 카탈로그를 열고 닫은 것까지 설치로 잡힌다.
		if (UFFZSWorldSubsystem* ZoneWorld = World->GetSubsystem<UFFZSWorldSubsystem>())
		{
			ZoneWorld->OnWorldChanged.AddDynamic(this, &UFF_AudioEventLink::HandleWorldChanged);
			ZoneWorld->OnPlacementRejected.AddDynamic(this, &UFF_AudioEventLink::HandlePlacementRejected);
			bBoundZoneWorld = true;
		}
	}

	if (!bBoundQueue)
	{
		if (UFF_QueueSubsystem* Queue = World->GetSubsystem<UFF_QueueSubsystem>())
		{
			Queue->OnPartyJoined.AddDynamic(this, &UFF_AudioEventLink::HandleQueuePartyJoined);
			Queue->OnPartyRejected.AddDynamic(this, &UFF_AudioEventLink::HandleQueuePartyRejected);
			Queue->OnServiceStarted.AddDynamic(this, &UFF_AudioEventLink::HandleQueueServiceStarted);
			Queue->OnPartyExited.AddDynamic(this, &UFF_AudioEventLink::HandleQueuePartyExited);
			bBoundQueue = true;
		}
	}
}

void UFF_AudioEventLink::UnbindAll()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		bBoundFinale = false;
		bBoundQueue = false;
		bBoundStage = false;
		bBoundBuild = false;
		bBoundZoneWorld = false;
		return;
	}

	if (bBoundFinale)
	{
		if (UFF_FinaleShowSubsystem* Finale = World->GetSubsystem<UFF_FinaleShowSubsystem>())
		{
			Finale->OnBurst.RemoveDynamic(this, &UFF_AudioEventLink::HandleFinaleBurst);
			Finale->OnShowStateChanged.RemoveDynamic(this, &UFF_AudioEventLink::HandleFinaleShowStateChanged);
			Finale->OnLaunch.RemoveDynamic(this, &UFF_AudioEventLink::HandleFinaleLaunch);
		}
		bBoundFinale = false;
	}

	if (bBoundStage)
	{
		if (UFF_FestivalStageSubsystem* Stage =
			UGameInstance::GetSubsystem<UFF_FestivalStageSubsystem>(World->GetGameInstance()))
		{
			Stage->OnStageChanged.RemoveDynamic(this, &UFF_AudioEventLink::HandleStageChanged);
			Stage->OnEnvironmentStateChanged.RemoveDynamic(this, &UFF_AudioEventLink::HandleEnvironmentChanged);
		}
		bBoundStage = false;
	}

	if (bBoundBuild)
	{
		if (UFF_BuildModeSubsystem* Build = World->GetSubsystem<UFF_BuildModeSubsystem>())
		{
			Build->OnBuildModeStateChanged.RemoveDynamic(this, &UFF_AudioEventLink::HandleBuildModeStateChanged);
		}
		bBoundBuild = false;
	}

	if (bBoundZoneWorld)
	{
		if (UFFZSWorldSubsystem* ZoneWorld = World->GetSubsystem<UFFZSWorldSubsystem>())
		{
			ZoneWorld->OnWorldChanged.RemoveDynamic(this, &UFF_AudioEventLink::HandleWorldChanged);
			ZoneWorld->OnPlacementRejected.RemoveDynamic(this, &UFF_AudioEventLink::HandlePlacementRejected);
		}
		bBoundZoneWorld = false;
	}

	if (bBoundQueue)
	{
		if (UFF_QueueSubsystem* Queue = World->GetSubsystem<UFF_QueueSubsystem>())
		{
			Queue->OnPartyJoined.RemoveDynamic(this, &UFF_AudioEventLink::HandleQueuePartyJoined);
			Queue->OnPartyRejected.RemoveDynamic(this, &UFF_AudioEventLink::HandleQueuePartyRejected);
			Queue->OnServiceStarted.RemoveDynamic(this, &UFF_AudioEventLink::HandleQueueServiceStarted);
			Queue->OnPartyExited.RemoveDynamic(this, &UFF_AudioEventLink::HandleQueuePartyExited);
		}
		bBoundQueue = false;
	}
}

void UFF_AudioEventLink::HandleFinaleBurst(const FVector WorldLocation, const FLinearColor Color)
{
	(void)Color;

	if (!bLinkEnabled)
	{
		return;
	}

	// 터진 자리에서 소리를 낸다. 멀리서 터지면 작게 들려야 거리감이 산다.
	PlayFinaleEventWithTravelDelay(EFF_AudioEvent::FinaleBurst, WorldLocation);
}

void UFF_AudioEventLink::HandleFinaleLaunch(const FVector LaunchLocation, const FVector BurstLocation, const float RiseSeconds)
{
	(void)BurstLocation;
	(void)RiseSeconds;

	if (!bLinkEnabled)
	{
		return;
	}

	// 바다 위 바지선에서 쿵. 뱅크에 소리가 없으면 조용히 넘어간다(오디오 서브시스템).
	PlayFinaleEventWithTravelDelay(EFF_AudioEvent::FinaleLaunch, LaunchLocation);
}

void UFF_AudioEventLink::PlayFinaleEventWithTravelDelay(const EFF_AudioEvent Event, const FVector& WorldLocation)
{
	UWorld* World = GetWorld();
	const float Delay = ResolveBurstSoundDelay(WorldLocation);
	if (!World || Delay <= 0.02f)
	{
		if (UFF_AudioSubsystem* Audio = GetAudio())
		{
			Audio->PlayEventAtLocation(Event, WorldLocation);
		}
		return;
	}

	/* 소리가 날아오는 시간만큼 뒤에 낸다(FF_AudioSettings 주석).

	   타이머는 월드가 소유해서 레벨이 바뀌면 같이 사라진다. 연출을 중간에 멈춰도 이미 터진
	   불꽃의 소리는 늦게라도 들리는 게 맞으므로 따로 취소하지 않는다. 결선을 끈 뒤에는 내지 않는다.
	*/
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateWeakLambda(this, [this, Event, WorldLocation]()
		{
			if (!bLinkEnabled)
			{
				return;
			}
			if (UFF_AudioSubsystem* Audio = GetAudio())
			{
				Audio->PlayEventAtLocation(Event, WorldLocation);
			}
		}),
		Delay, false);
}

float UFF_AudioEventLink::ResolveBurstSoundDelay(const FVector& WorldLocation) const
{
	const UFF_AudioSettings* Settings = UFF_AudioSettings::Get();
	const UWorld* World = GetWorld();
	if (!Settings || !Settings->bDelayFinaleBurstBySoundTravel || !World)
	{
		return 0.f;
	}

	// 듣는 사람 자리는 오디오 리스너다. 카메라가 순회 중이면 그 시점에서 잰다.
	const APlayerController* Controller = World->GetFirstPlayerController();
	if (!Controller)
	{
		return 0.f;
	}

	FVector ListenerLocation = FVector::ZeroVector;
	FVector FrontDir = FVector::ForwardVector;
	FVector RightDir = FVector::RightVector;
	Controller->GetAudioListenerPosition(ListenerLocation, FrontDir, RightDir);

	return UFF_AudioSettings::ComputeSoundTravelDelay(
		FVector::Dist(ListenerLocation, WorldLocation),
		Settings->SpeedOfSoundCmPerSecond,
		Settings->MaxFinaleBurstSoundDelaySeconds);
}

void UFF_AudioEventLink::HandleFinaleShowStateChanged(const bool bInProgress)
{
	if (!bLinkEnabled)
	{
		return;
	}

	UFF_AudioSubsystem* Audio = GetAudio();
	if (!Audio)
	{
		return;
	}

	if (bInProgress)
	{
		// 등급은 연출이 시작 신호를 보내기 전에 정해져 있다(UFF_FinaleShowSubsystem::StartShowWithGrade).
		Audio->PlayEvent2D(EFF_AudioEvent::FinaleShowStarted);
		Audio->SetAmbience(ResolveFinaleAmbience(*Audio));
		return;
	}

	Audio->PlayEvent2D(EFF_AudioEvent::FinaleShowEnded);

	// 연출이 끝나면 배경음을 되돌린다. 어느 단계로 갈지는 진행 쪽이 알고 있으므로
	// 여기서는 피날레 곡만 걷어내고 결산 곡을 건다. 폐장 곡이 없으면 플레이리스트로 돌아간다.
	if (IsFinaleAmbience(Audio->GetCurrentAmbience()))
	{
		Audio->SetAmbience(ResolveStageAmbience(EFF_FestivalStage::Closing, bLastIsNight,
			Audio->HasPlaylist(), Audio->HasAmbienceTrack(EFF_AudioAmbience::Closing)));
	}
}

EFF_AudioAmbience UFF_AudioEventLink::ResolveShowAmbience(const EFF_FinaleGrade Grade, const bool bHasDedicatedTrack)
{
	return (Grade == EFF_FinaleGrade::S && bHasDedicatedTrack) ? EFF_AudioAmbience::FinaleEnding : EFF_AudioAmbience::Finale;
}

EFF_AudioAmbience UFF_AudioEventLink::ResolveFinaleAmbience(const UFF_AudioSubsystem& Audio) const
{
	EFF_FinaleGrade Grade = EFF_FinaleGrade::None;
	const UWorld* World = GetWorld();
	const UFF_FinaleShowSubsystem* Finale = World ? World->GetSubsystem<UFF_FinaleShowSubsystem>() : nullptr;
	if (Finale && Finale->IsShowInProgress())
	{
		Grade = Finale->GetActiveGrade();
	}
	else if (const UFF_FestivalProgressionSubsystem* Progression = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UFF_FestivalProgressionSubsystem>()
		: nullptr)
	{
		Grade = Progression->GetPurchasedFinaleGrade();
	}

	return ResolveShowAmbience(Grade, Audio.HasAmbienceTrack(EFF_AudioAmbience::FinaleEnding));
}

bool UFF_AudioEventLink::IsFinaleAmbience(const EFF_AudioAmbience Ambience)
{
	return Ambience == EFF_AudioAmbience::Finale || Ambience == EFF_AudioAmbience::FinaleEnding;
}

void UFF_AudioEventLink::HandleQueuePartyJoined(
	const int32 FacilityId, const int32 PartyId, const int32 SlotIndex, const float EstimatedWaitSeconds)
{
	(void)FacilityId; (void)PartyId; (void)SlotIndex; (void)EstimatedWaitSeconds;

	if (!bLinkEnabled)
	{
		return;
	}

	// 위치를 모르므로 2D 로 낸다. 좌표가 필요해지면 이벤트에 담아야 하는데,
	// 그건 Queue 계약을 바꾸는 일이라 소리 하나 때문에 할 일은 아니다.
	if (UFF_AudioSubsystem* Audio = GetAudio())
	{
		Audio->PlayEvent2D(EFF_AudioEvent::QueueJoined);
	}
}

void UFF_AudioEventLink::HandleQueuePartyRejected(
	const int32 FacilityId, const int32 PartyId, const EFF_QueueJoinResult Reason)
{
	(void)FacilityId; (void)PartyId; (void)Reason;

	if (!bLinkEnabled)
	{
		return;
	}

	if (UFF_AudioSubsystem* Audio = GetAudio())
	{
		Audio->PlayEvent2D(EFF_AudioEvent::QueueRejected);
	}
}

void UFF_AudioEventLink::HandleQueueServiceStarted(
	const int32 FacilityId, const int32 PartyId, const float WaitedSeconds)
{
	(void)FacilityId; (void)PartyId; (void)WaitedSeconds;

	if (!bLinkEnabled)
	{
		return;
	}

	if (UFF_AudioSubsystem* Audio = GetAudio())
	{
		Audio->PlayEvent2D(EFF_AudioEvent::QueueServiceStarted);
	}
}

void UFF_AudioEventLink::HandleQueuePartyExited(
	const int32 FacilityId, const int32 PartyId, const EFF_QueueExit ExitType, const float WaitedSeconds)
{
	(void)FacilityId; (void)PartyId; (void)WaitedSeconds;

	if (!bLinkEnabled)
	{
		return;
	}

	UFF_AudioSubsystem* Audio = GetAudio();
	if (!Audio)
	{
		return;
	}

	// 서비스를 받고 나간 것과 기다리다 포기한 것은 다른 소리여야 한다.
	// 둘을 같은 소리로 내면 플레이어가 시설이 잘 돌아가는지 알 수 없다.
	switch (ExitType)
	{
	case EFF_QueueExit::Served:
		Audio->PlayEvent2D(EFF_AudioEvent::QueueServed);
		break;
	case EFF_QueueExit::Abandoned:
		Audio->PlayEvent2D(EFF_AudioEvent::QueueAbandoned);
		break;
	default:
		// 폐쇄로 인한 취소는 플레이어 잘못이 아니므로 별도 소리를 내지 않는다.
		break;
	}
}

void UFF_AudioEventLink::HandleStageChanged(
	const EFF_FestivalStage PreviousStage, const EFF_FestivalStage NewStage)
{
	(void)PreviousStage;

	LastStage = NewStage;

	if (!bLinkEnabled)
	{
		return;
	}

	if (UFF_AudioSubsystem* Audio = GetAudio())
	{
		Audio->PlayEvent2D(EFF_AudioEvent::StageChanged);
	}

	RefreshAmbience();
}

void UFF_AudioEventLink::HandleEnvironmentChanged(const EFF_WeatherType Weather, const bool bIsNight)
{
	(void)Weather;

	// 같은 값이 다시 오면 넘긴다. 날씨만 바뀌었는데 배경음을 다시 걸 이유가 없다.
	if (bLastIsNight == bIsNight)
	{
		return;
	}
	bLastIsNight = bIsNight;

	if (!bLinkEnabled)
	{
		return;
	}

	if (UFF_AudioSubsystem* Audio = GetAudio())
	{
		Audio->PlayEvent2D(bIsNight ? EFF_AudioEvent::NightStarted : EFF_AudioEvent::DayStarted);
	}

	RefreshAmbience();
}

void UFF_AudioEventLink::RefreshAmbience()
{
	if (!bLinkEnabled)
	{
		return;
	}

	UFF_AudioSubsystem* Audio = GetAudio();
	if (!Audio)
	{
		return;
	}

	// 연출 중에는 건드리지 않는다. 피날레 곡이 걸려 있는데 단계 신호가 오면
	// 그 위에 덮어써서 연출이 끊긴다.
	if (IsFinaleAmbience(Audio->GetCurrentAmbience()))
	{
		return;
	}

	EFF_AudioAmbience Next = EFF_AudioAmbience::None;

	/* 로비는 단계와 상관없이 화면으로 곡을 정한다. 타이틀 화면은 타이틀 곡, 나머지는 로비 곡.

	   축제 단계는 게임 인스턴스가 들고 있어서 로비에 와도 값이 남는다. 그래서 로비에서는 단계를 보지 않는다.
	*/
	if (bIsLobbyWorld)
	{
		const EFF_AudioAmbience LobbyNext = ResolveLobbyAmbience(*Audio);
		if (LobbyNext != EFF_AudioAmbience::None)
		{
			Audio->SetAmbience(LobbyNext);
		}
		return;
	}

	const bool bHasClosingTrack = Audio->HasAmbienceTrack(EFF_AudioAmbience::Closing);
	if (!bHasClosingTrack && !bWarnedMissingClosingTrack
		&& (LastStage == EFF_FestivalStage::Closing || LastStage == EFF_FestivalStage::Report))
	{
		// 비워 둔 것이 의도일 수 있어 경고가 아니라 기록으로 한 번만 남긴다.
		bWarnedMissingClosingTrack = true;
		UE_LOG(LogFestaFiestaAudio, Log,
			TEXT("[사운드] 폐장 곡이 뱅크에 없어 결산 동안 플레이리스트를 이어 튼다. 따로 틀 곡이 있으면 배경음에 `정리 - 폐장` 을 넣을 것."));
	}

	Next = ResolveStageAmbience(LastStage, bLastIsNight, Audio->HasPlaylist(), bHasClosingTrack);

	// 피날레 단계는 등급으로 곡을 가른다(S 는 전용곡). 연출 시작 신호와 같은 곡이어야 처음이 안 섞인다.
	if (Next == EFF_AudioAmbience::Finale)
	{
		Next = ResolveFinaleAmbience(*Audio);
	}
	Audio->SetAmbience(Next);
}

EFF_AudioAmbience UFF_AudioEventLink::ResolveStageAmbience(
	const EFF_FestivalStage Stage, const bool bIsNight, const bool bHasPlaylist, const bool bHasClosingTrack)
{
	/* 준비·운영은 플레이리스트 하나로 흐른다.

	   예전에는 준비·낮·밤마다 곡이 갈렸는데, 낮밤이 빠르게 바뀌어 곡이 자주 끊겼다(피드백).
	   같은 곡을 다시 요청하면 무시되므로 단계나 밤낮이 바뀌어도 듣던 곡이 이어진다.
	   플레이리스트가 비어 있으면 예전 규칙으로 간다.
	*/
	switch (Stage)
	{
	case EFF_FestivalStage::Setup:
	case EFF_FestivalStage::MorningCheck:
	case EFF_FestivalStage::PromotionPreview:
		return bHasPlaylist ? EFF_AudioAmbience::Playlist : EFF_AudioAmbience::Setup;

	case EFF_FestivalStage::Live:
	case EFF_FestivalStage::SunsetTransition:
		// 플레이리스트가 없으면 밤낮으로 갈린다(예전 규칙).
		if (bHasPlaylist)
		{
			return EFF_AudioAmbience::Playlist;
		}
		return bIsNight ? EFF_AudioAmbience::NightLive : EFF_AudioAmbience::DayLive;

	case EFF_FestivalStage::Finale:
		return EFF_AudioAmbience::Finale;

	case EFF_FestivalStage::Closing:
	case EFF_FestivalStage::Report:
		// 폐장 곡이 있으면 그 곡. 없으면 듣던 플레이리스트를 끊지 않는다.
		// 둘 다 없으면 예전처럼 폐장으로 간다(지금 곡만 꺼진다).
		if (!bHasClosingTrack && bHasPlaylist)
		{
			return EFF_AudioAmbience::Playlist;
		}
		return EFF_AudioAmbience::Closing;

	default:
		return EFF_AudioAmbience::None;
	}
}

EFF_AudioAmbience UFF_AudioEventLink::ResolveLobbyAmbience(const UFF_AudioSubsystem& Audio) const
{
	const UFF_UIFlowSubsystem* Flow = UFF_UIFlowSubsystem::Get(this);
	const EFF_UIFlowScreen Screen = Flow ? Flow->GetFrontendScreen() : EFF_UIFlowScreen::None;
	const bool bSomethingPlaying = Audio.GetCurrentAmbience() != EFF_AudioAmbience::None;

	switch (Screen)
	{
	case EFF_UIFlowScreen::Title:
		// 타이틀 곡을 아직 안 넣었으면 로비 곡으로 둔다(예전 동작).
		if (Audio.HasAmbienceTrack(EFF_AudioAmbience::Title))
		{
			return EFF_AudioAmbience::Title;
		}
		break;

	case EFF_UIFlowScreen::SaveList:
	case EFF_UIFlowScreen::Options:
		// 타이틀과 로비 양쪽에서 열린다. 열기 전에 흐르던 곡을 그대로 둔다.
		if (bSomethingPlaying)
		{
			return EFF_AudioAmbience::None;
		}
		break;

	case EFF_UIFlowScreen::None:
		/* 화면이 아직 없다. 시작 영상 중이거나 레벨 전환 직후다.

		   여기서 곡을 정하면 곧 뜰 화면과 다른 곡이 잠깐 흐른다. 화면을 기다리되, 흐름 없이
		   레벨을 직접 열어 화면이 끝내 안 뜨면 로비 곡을 튼다(PollFrontendScreen).
		*/
		if (bSomethingPlaying || !bFrontendScreenWaitExpired)
		{
			return EFF_AudioAmbience::None;
		}
		break;

	default:
		// 플레이 허브, 지구본, 국가 지도.
		break;
	}

	if (Audio.HasAmbienceTrack(EFF_AudioAmbience::Lobby))
	{
		return EFF_AudioAmbience::Lobby;
	}

	// 로비 곡을 아직 뱅크에 안 넣었으면 예전처럼 개장 전 곡으로 둔다.
	// 곡을 넣기 전까지 로비가 조용해지면 소리가 고장 난 것처럼 보인다.
	if (!bWarnedMissingLobbyTrack)
	{
		bWarnedMissingLobbyTrack = true;
		UE_LOG(LogFestaFiestaAudio, Warning,
			TEXT("[사운드] 로비 곡이 뱅크에 없어 개장 전 곡을 대신 튼다. 오디오 뱅크의 배경음에 `로비` 를 넣을 것."));
	}
	return EFF_AudioAmbience::Setup;
}

void UFF_AudioEventLink::PollFrontendScreen()
{
	const UFF_UIFlowSubsystem* Flow = UFF_UIFlowSubsystem::Get(this);
	const EFF_UIFlowScreen Screen = Flow ? Flow->GetFrontendScreen() : EFF_UIFlowScreen::None;

	// 화면 없이 오래 기다렸으면 로비 곡으로 정한다. 한 번만.
	if (Screen == EFF_UIFlowScreen::None && !bFrontendScreenWaitExpired)
	{
		FrontendScreenWaitSeconds += 0.2f;
		if (FrontendScreenWaitSeconds >= 2.f)
		{
			bFrontendScreenWaitExpired = true;
			RefreshAmbience();
		}
	}

	const uint8 ScreenValue = static_cast<uint8>(Screen);
	if (ScreenValue == LastFrontendScreen)
	{
		return;
	}
	LastFrontendScreen = ScreenValue;

	RefreshAmbience();
}

void UFF_AudioEventLink::TryCreatePlaylistPlayer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UFF_AudioSettings* Settings = UFF_AudioSettings::Get();
	UClass* WidgetClass = Settings ? Settings->PlaylistPlayerWidgetClass.LoadSynchronous() : nullptr;
	if (!WidgetClass || PlaylistPlayerWidget)
	{
		// 클래스가 없으면(아직 WBP 를 안 만들었으면) 조용히 그만둔다. 소리는 그대로 돈다.
		if (!WidgetClass && !PlaylistPlayerWidget)
		{
			UE_LOG(LogFestaFiestaAudio, Log,
				TEXT("[사운드] 플레이리스트 플레이어 WBP 가 없어 띄우지 않는다. 에디터 콘솔에서 FF.PlaylistPlayerUMG 를 실행할 것."));
		}
		World->GetTimerManager().ClearTimer(PlaylistPlayerCreateTimer);
		return;
	}

	APlayerController* Controller = World->GetFirstPlayerController();
	if (!Controller || !Controller->IsLocalController())
	{
		// 아직 플레이어가 없다. 다음 시도에서 다시 본다.
		return;
	}

	PlaylistPlayerWidget = CreateWidget<UUserWidget>(Controller, WidgetClass);
	if (PlaylistPlayerWidget)
	{
		PlaylistPlayerWidget->AddToViewport(Settings->PlaylistPlayerZOrder);
	}
	World->GetTimerManager().ClearTimer(PlaylistPlayerCreateTimer);
}

void UFF_AudioEventLink::HandleBuildModeStateChanged(
	const EFF_BuildModeState PreviousState, const EFF_BuildModeState NewState)
{
	if (!bLinkEnabled)
	{
		return;
	}

	UFF_AudioSubsystem* Audio = GetAudio();
	if (!Audio)
	{
		return;
	}

	// 카탈로그를 열고 닫는 소리다. 실제 설치·철거음은 Zone 이 확정한 뒤
	// HandleWorldChanged 에서 낸다.
	const bool bWasClosed = PreviousState == EFF_BuildModeState::Closed;
	const bool bIsClosed = NewState == EFF_BuildModeState::Closed;

	if (bWasClosed && !bIsClosed)
	{
		Audio->PlayEvent2D(EFF_AudioEvent::UiConfirm);
		return;
	}

	if (!bWasClosed && bIsClosed)
	{
		Audio->PlayEvent2D(EFF_AudioEvent::UiCancel);
	}
}

void UFF_AudioEventLink::HandleWorldChanged(const FFFZSWorldChangeEvent& Event)
{
	if (!bLinkEnabled)
	{
		return;
	}

	UFF_AudioSubsystem* Audio = GetAudio();
	if (!Audio)
	{
		return;
	}

	// 종류마다 다른 소리를 낸다. 설치와 철거가 같은 소리면 뭘 했는지 귀로 구분이 안 된다.
	EFF_AudioEvent Sound = EFF_AudioEvent::None;

	switch (Event.Type)
	{
	case EFFZSCommandType::Install:        Sound = EFF_AudioEvent::BuildPlaced;         break;
	case EFFZSCommandType::Move:           Sound = EFF_AudioEvent::BuildMoved;          break;
	case EFFZSCommandType::Remove:         Sound = EFF_AudioEvent::BuildRemoved;        break;
	case EFFZSCommandType::Open:           Sound = EFF_AudioEvent::BuildFacilityOpened; break;
	case EFFZSCommandType::Close:
	case EFFZSCommandType::EmergencyClose: Sound = EFF_AudioEvent::BuildFacilityClosed; break;
	default: return;
	}

	// 위치를 모르므로 2D 로 낸다. 시설 좌표를 얻으려면 Zone 에 다시 물어야 하는데,
	// 소리 하나 때문에 그 조회를 매번 하는 건 과하다.
	Audio->PlayEvent2D(Sound);
}

void UFF_AudioEventLink::HandlePlacementRejected(
	const FFFZSCommand& Command, const FFFZSCommandResult& Result)
{
	(void)Command; (void)Result;

	if (!bLinkEnabled)
	{
		return;
	}

	if (UFF_AudioSubsystem* Audio = GetAudio())
	{
		Audio->PlayEvent2D(EFF_AudioEvent::BuildRejected);
	}
}
