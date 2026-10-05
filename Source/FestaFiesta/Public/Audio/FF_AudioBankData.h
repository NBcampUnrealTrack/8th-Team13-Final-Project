// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Audio/FF_AudioTypes.h"
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FF_AudioBankData.generated.h"

// 어떤 순간에 어떤 소리를 낼지 모아둔 표.
//
// 애셋을 코드가 아니라 데이터로 두는 이유는, 소리는 들어보고 정해야 하는 값이기 때문이다.
// 볼륨이 조금 큰지 음높이가 어색한지는 화면 앞에서만 알 수 있고, 그때마다 빌드를 다시
// 하고 싶지 않다.
//
// 비어 있는 항목은 그냥 소리가 안 난다. 애셋이 아직 없는 단계에서도 게임이 돌아야 한다.
UCLASS(BlueprintType)
class FESTAFIESTA_API UFF_AudioBankData : public UDataAsset
{
	GENERATED_BODY()

public:
	// 순간별 효과음.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TMap<EFF_AudioEvent, FFF_AudioCue> Cues;

	// 상황별 배경음.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TMap<EFF_AudioAmbience, FFF_AudioAmbienceTrack> Ambiences;

	/* 축제 플레이리스트. 준비·운영(낮/밤) 내내 위에서부터 차례로 튼다. 끝까지 가면 처음으로.

	   곡의 Looping 은 꺼 둔다. 켜져 있으면 곡 길이만큼 지난 뒤 코드가 다음 곡으로 넘기지만,
	   곡 끝이 정확히 맞지 않을 수 있다. 볼륨은 배경음 슬라이더를 따른다.
	   비워두면 예전처럼 Ambiences 의 준비·낮·밤 곡을 쓴다.

	   곡을 넣을 때 Display Name 에 플레이어에 보일 이름을 적는다(한글 그대로 된다). 애셋 이름은
	   영문으로 두고 보이는 이름만 여기서 정한다 — 애셋 이름을 한글로 바꾸면 패키징·Git 에서 경로가
	   깨질 수 있다. 목록의 줄 제목이 이 이름이라 어느 곡에 이름을 안 넣었는지 바로 보인다.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Playlist", meta = (TitleProperty = "DisplayName"))
	TArray<FFF_AudioAmbienceTrack> FestivalPlaylist;

	// 켜면 한 바퀴마다 순서를 섞는다. 바퀴가 바뀔 때 같은 곡이 연달아 나오지는 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Playlist")
	bool bShufflePlaylist = false;

	// 없으면 nullptr. 호출부가 조용히 넘어간다.
	const FFF_AudioCue* FindCue(EFF_AudioEvent Event) const
	{
		return Cues.Find(Event);
	}

	const FFF_AudioAmbienceTrack* FindAmbience(EFF_AudioAmbience Ambience) const
	{
		return Ambiences.Find(Ambience);
	}
};
