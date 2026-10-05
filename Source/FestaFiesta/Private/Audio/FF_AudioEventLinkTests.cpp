// Copyright Epic Games, Inc. All Rights Reserved.

// 역할: 단계별 배경음 규칙. 결산(마감·리포트)에 틀 곡이 있으면 무음이 되지 않는지, 피날레 곡이 등급으로 갈리는지 본다.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Audio/FF_AudioEventLink.h"
#include "Finale/FF_FinaleShowTypes.h"

namespace
{
	// 그 곡을 실제로 틀 수 있는가. 플레이리스트·폐장 말고는 이 시험의 관심 밖이라 참으로 둔다.
	bool IsPlayable(const EFF_AudioAmbience Ambience, const bool bHasPlaylist, const bool bHasClosingTrack)
	{
		switch (Ambience)
		{
		case EFF_AudioAmbience::Playlist: return bHasPlaylist;
		case EFF_AudioAmbience::Closing:  return bHasClosingTrack;
		case EFF_AudioAmbience::None:     return false;
		default:                          return true;
		}
	}
}

// 결산 단계는 틀 곡이 하나라도 있으면 틀 수 있는 곡을 고른다. 폐장 곡이 있으면 그것이 먼저다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFF_AudioClosingKeepsMusic, "FestaFiesta.Audio.EventLink.ClosingKeepsMusic", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FFF_AudioClosingKeepsMusic::RunTest(const FString& Parameters)
{
	for (const EFF_FestivalStage Stage : { EFF_FestivalStage::Closing, EFF_FestivalStage::Report })
	{
		for (const bool bNight : { false, true })
		{
			for (const bool bPlaylist : { false, true })
			{
				for (const bool bClosing : { false, true })
				{
					const EFF_AudioAmbience Picked =
						UFF_AudioEventLink::ResolveStageAmbience(Stage, bNight, bPlaylist, bClosing);
					const FString Case = FString::Printf(TEXT("단계 %d 밤 %d 플레이리스트 %d 폐장곡 %d"),
						static_cast<int32>(Stage), bNight, bPlaylist, bClosing);

					if (bPlaylist || bClosing)
					{
						TestTrue(Case + TEXT(": 틀 곡이 있으면 무음이 아니다"), IsPlayable(Picked, bPlaylist, bClosing));
					}
					if (bClosing)
					{
						TestEqual(Case + TEXT(": 폐장 곡이 있으면 그 곡"), Picked, EFF_AudioAmbience::Closing);
					}
				}
			}
		}
	}
	return true;
}

// 폐장 곡이 없으면 운영 → 마감 → 리포트 → 다음 날 아침 내내 같은 배경음이라 곡이 끊기지 않는다.
// 같은 배경음을 다시 요청하면 오디오 쪽이 무시한다(UFF_AudioSubsystem::SetAmbience).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFF_AudioDayBoundaryIsSeamless, "FestaFiesta.Audio.EventLink.DayBoundaryIsSeamless", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FFF_AudioDayBoundaryIsSeamless::RunTest(const FString& Parameters)
{
	const EFF_FestivalStage Day[] = {
		EFF_FestivalStage::Live, EFF_FestivalStage::Closing,
		EFF_FestivalStage::Report, EFF_FestivalStage::MorningCheck
	};

	for (const bool bNight : { false, true })
	{
		const EFF_AudioAmbience First = UFF_AudioEventLink::ResolveStageAmbience(Day[0], bNight, true, false);
		for (const EFF_FestivalStage Stage : Day)
		{
			TestEqual(FString::Printf(TEXT("단계 %d 밤 %d 은 운영과 같은 배경음"), static_cast<int32>(Stage), bNight),
				UFF_AudioEventLink::ResolveStageAmbience(Stage, bNight, true, false), First);
		}
	}

	// 폐장 곡을 넣으면 결산에서만 갈아탄다.
	TestNotEqual(TEXT("폐장 곡이 있으면 결산은 운영과 다른 곡"),
		UFF_AudioEventLink::ResolveStageAmbience(EFF_FestivalStage::Report, false, true, true),
		UFF_AudioEventLink::ResolveStageAmbience(EFF_FestivalStage::Live, false, true, true));
	return true;
}

// S 등급은 전용곡 하나, A 등급 이하는 피날레 곡. 전용곡이 비어 있으면 S 도 피날레 곡.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFF_AudioFinaleSongByGrade, "FestaFiesta.Audio.EventLink.FinaleSongByGrade", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FFF_AudioFinaleSongByGrade::RunTest(const FString& Parameters)
{
	for (const EFF_FinaleGrade Grade : FFF_FinaleGrades::GetAscendingOrder())
	{
		const FString Case = FString::Printf(TEXT("등급 %d"), static_cast<int32>(Grade));
		const EFF_AudioAmbience WithDedicated = UFF_AudioEventLink::ResolveShowAmbience(Grade, true);
		TestEqual(Case + TEXT(": 전용곡이 있으면 S 만 전용곡"), WithDedicated,
			Grade == EFF_FinaleGrade::S ? EFF_AudioAmbience::FinaleEnding : EFF_AudioAmbience::Finale);
		TestEqual(Case + TEXT(": 전용곡이 없으면 피날레 곡"),
			UFF_AudioEventLink::ResolveShowAmbience(Grade, false), EFF_AudioAmbience::Finale);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
