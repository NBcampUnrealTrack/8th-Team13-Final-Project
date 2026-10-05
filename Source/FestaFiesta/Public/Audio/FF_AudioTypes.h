// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FF_AudioTypes.generated.h"

class USoundBase;
class USoundConcurrency;

// 축제 사운드 관련 타입.
//
// 미니게임 오디오와는 별개다. 저쪽은 세션 단위라 게임이 끝나면 소리도 끝나는데,
// 축제 배경음은 계속 흐르면서 단계에 따라 바뀌어야 해서 모델이 다르다.

DECLARE_LOG_CATEGORY_EXTERN(LogFestaFiestaAudio, Log, All);

// 소리가 나야 하는 순간들.
//
// 번호를 띄엄띄엄 둔 이유는 중간에 끼워 넣을 때 기존 값이 밀리지 않게 하기 위해서다.
// 데이터 애셋이 이 값을 키로 저장하므로 번호가 바뀌면 매핑이 통째로 어긋난다.
UENUM(BlueprintType)
enum class EFF_AudioEvent : uint8
{
	None = 0                UMETA(Hidden),

	// ── 피날레 ──────────────────────────────────────────────────────────────
	FinaleBurst = 10        UMETA(DisplayName = "피날레 - 불꽃 터짐"),
	FinaleShowStarted = 11  UMETA(DisplayName = "피날레 - 연출 시작"),
	FinaleShowEnded = 12    UMETA(DisplayName = "피날레 - 연출 종료"),

	// 바지선 발사통에서 알을 쏘아 올리는 쿵 소리. 발사 자리에서 나고, 소리가 날아오는 만큼 늦게 들린다.
	FinaleLaunch = 13       UMETA(DisplayName = "피날레 - 발사"),

	// ── 대기열 ──────────────────────────────────────────────────────────────
	QueueJoined = 30        UMETA(DisplayName = "대기열 - 줄 서기"),
	QueueRejected = 31      UMETA(DisplayName = "대기열 - 가입 거절"),
	QueueServiceStarted = 32 UMETA(DisplayName = "대기열 - 서비스 시작"),
	QueueServed = 33        UMETA(DisplayName = "대기열 - 이용 완료"),
	QueueAbandoned = 34     UMETA(DisplayName = "대기열 - 대기 포기"),

	// ── 건축 ────────────────────────────────────────────────────────────────
	BuildPlaced = 50        UMETA(DisplayName = "건축 - 시설 설치"),
	BuildRemoved = 51       UMETA(DisplayName = "건축 - 시설 철거"),
	BuildRejected = 52      UMETA(DisplayName = "건축 - 배치 불가"),
	BuildMoved = 53         UMETA(DisplayName = "건축 - 시설 이동"),
	BuildFacilityOpened = 54 UMETA(DisplayName = "건축 - 시설 개장"),
	BuildFacilityClosed = 55 UMETA(DisplayName = "건축 - 시설 폐장"),

	// ── UI ──────────────────────────────────────────────────────────────────
	UiClick = 70            UMETA(DisplayName = "UI - 클릭"),
	UiHover = 71            UMETA(DisplayName = "UI - 마우스 오버"),
	UiConfirm = 72          UMETA(DisplayName = "UI - 확인"),
	UiCancel = 73           UMETA(DisplayName = "UI - 취소"),
	UiError = 74            UMETA(DisplayName = "UI - 오류"),

	// ── 축제 진행 ───────────────────────────────────────────────────────────
	StageChanged = 90       UMETA(DisplayName = "진행 - 단계 전환"),
	DayStarted = 91         UMETA(DisplayName = "진행 - 낮 시작"),
	NightStarted = 92       UMETA(DisplayName = "진행 - 밤 시작")
};

// 배경음 갈래. 상황에 따라 크로스페이드로 갈아탄다.
UENUM(BlueprintType)
enum class EFF_AudioAmbience : uint8
{
	None = 0        UMETA(DisplayName = "없음"),
	Setup           UMETA(DisplayName = "준비 - 개장 전"),
	DayLive         UMETA(DisplayName = "축제 - 낮"),
	NightLive       UMETA(DisplayName = "축제 - 밤"),
	// A 등급 이하 피날레 곡. S 등급은 아래 전용곡을 쓴다.
	Finale          UMETA(DisplayName = "피날레 (A등급 이하)"),
	Closing         UMETA(DisplayName = "정리 - 폐장"),

	/* S 등급 피날레 전용곡. S 불꽃이 시작될 때부터 연출이 끝날 때까지 이 곡 하나로 흐른다.
	   A 등급 이하는 피날레 곡(Finale)을 쓴다. 카메라 순회 여부와 상관없이 등급으로만 가른다.

	   예전에는 3막의 축제장 순회로 넘어갈 때 피날레 곡에서 이 곡으로 갈아타 곡이 끊겼다(2026-10-05 피드백).
	   비워 두면 S 도 피날레 곡을 쓴다. 맨 뒤에 붙인 이유는 뱅크가 이 값을 키로 저장해서,
	   중간에 끼우면 뒤의 곡이 한 칸씩 밀리기 때문이다(이름은 예전 그대로 둔다).
	*/
	FinaleEnding    UMETA(DisplayName = "피날레 - S등급 전용곡"),

	// 로비(프론트엔드 레벨: 타이틀, 지구본, 국가 지도) 전용 곡. 개장 전 곡과 따로 둔다.
	// 예전에는 로비에서도 축제 단계가 기본값(준비)이라 개장 전 곡이 흘러, 로비와 축제 준비가
	// 한 곡으로 이어졌다. 뱅크 키가 밀리지 않게 맨 뒤에 붙인다.
	Lobby           UMETA(DisplayName = "로비"),

	/* 타이틀 화면 전용 곡. 로비(플레이 허브·지구본·국가 지도)와 따로 둔다.

	   로비와 타이틀은 같은 프론트엔드 레벨이라 레벨로는 가를 수 없다. 화면(EFF_UIFlowScreen)으로
	   가른다(UFF_AudioEventLink). 예전에는 둘 다 로비 곡이라 타이틀 곡이 로비까지 이어졌다(피드백).
	   뱅크에 없으면 로비 곡을 대신 튼다. 뱅크 키가 밀리지 않게 맨 뒤에 붙인다.
	*/
	Title           UMETA(DisplayName = "타이틀"),

	/* 축제 플레이리스트. 준비·운영(낮/밤) 내내 뱅크의 Festival Playlist 를 차례로 튼다.

	   뱅크의 배경음 표(Ambiences)에 넣는 키가 아니다. 곡 목록은 뱅크의 Festival Playlist 에 넣는다.
	   예전에는 단계·밤낮마다 곡이 갈렸는데, 낮밤이 빠르게 바뀌어 곡이 자주 끊겼다(피드백).
	   플레이리스트가 비어 있으면 예전처럼 준비·낮·밤 곡을 쓴다.
	*/
	Playlist        UMETA(DisplayName = "축제 플레이리스트 (뱅크 키 아님)")
};

// 소리 갈래. 설정 창의 음량 슬라이더와 짝을 이룬다(FFF_AudioVolumes).
//
// 뱅크에서 소리마다 고를 수 있다. 기본값(효과음)으로 둔 소리는 이벤트 종류로 갈래를
// 정한다(UFF_AudioSubsystem::ResolveChannel). 소리를 넣을 때마다 칸을 고르지 않아도
// 불꽃은 불꽃놀이 슬라이더, 버튼 소리는 UI 슬라이더를 따르게 하려는 것이다.
UENUM(BlueprintType)
enum class EFF_AudioChannel : uint8
{
	Music       UMETA(DisplayName = "배경음"),
	Effects     UMETA(DisplayName = "효과음"),
	Ui          UMETA(DisplayName = "UI"),

	// 환경음은 따로 슬라이더를 두지 않고 효과음 슬라이더를 따른다. 아직 환경음 소리가 없어서
	// 빈 슬라이더가 되기 때문이다. 소리가 생기면 FFF_AudioVolumes 에 칸을 늘리면 된다.
	Ambience    UMETA(DisplayName = "환경음"),

	// 맨 뒤에 붙인다. 뱅크가 이 값을 숫자로 저장해서 중간에 끼우면 기존 소리의 갈래가 밀린다.
	Fireworks   UMETA(DisplayName = "불꽃놀이")
};

// 이벤트 하나에 붙는 소리 설정.
USTRUCT(BlueprintType)
struct FFF_AudioCue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TSoftObjectPtr<USoundBase> Sound;

	// 여럿 넣으면 무작위로 고른다. 같은 소리가 반복되면 금방 질린다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TArray<TSoftObjectPtr<USoundBase>> Variations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	EFF_AudioChannel Channel = EFF_AudioChannel::Effects;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float VolumeMultiplier = 1.f;

	// 재생마다 음높이를 조금씩 흔든다. 같은 소리를 연달아 낼 때 기계적으로 들리는 걸 막는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float PitchMin = 0.97f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float PitchMax = 1.03f;

	// 이 간격 안에 같은 이벤트가 또 오면 무시한다.
	//
	// 불꽃이 한 번에 여러 발 터지거나 NPC 여럿이 동시에 줄을 서면 같은 소리가 겹쳐서
	// 찢어지는 소리가 난다. 0 이면 제한하지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0"))
	float MinRetriggerSeconds = 0.04f;

	// 동시에 몇 개까지 울릴지. 지정하지 않으면 엔진 기본 정책을 따른다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TSoftObjectPtr<USoundConcurrency> Concurrency;

	bool IsValid() const
	{
		return !Sound.IsNull() || Variations.Num() > 0;
	}
};

// 배경음 하나.
USTRUCT(BlueprintType)
struct FFF_AudioAmbienceTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TSoftObjectPtr<USoundBase> Sound;

	/* 플레이리스트 플레이어에 보일 곡 이름. 한글을 그대로 적으면 된다.

	   비워두면 사운드 애셋 이름에서 만든다(밑줄은 공백으로). 애셋 이름은 영문이라 한글 제목은
	   여기에 적어야 나온다.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (DisplayName = "Display Name (곡 이름)"))
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float VolumeMultiplier = 1.f;

	// 갈아탈 때 걸리는 시간. 0 이면 즉시 바뀌는데, 그러면 뚝 끊겨서 귀에 거슬린다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0"))
	float FadeInSeconds = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0"))
	float FadeOutSeconds = 2.f;

	bool IsValid() const
	{
		return !Sound.IsNull();
	}
};
