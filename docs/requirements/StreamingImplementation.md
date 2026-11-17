# 스트리밍 구현 현황

갱신일: 2026-11-16

[방송 명세](YouTubeStreaming.md), [Data Routing](../../Engine/Data/DataRouting.md), [사이클과 명령](CycleAndCommands.md)에 맞춰 기반 코드와 설정 화면을 구현하고 있다. 아래의 라이브러리 구현과 실제 방송 연결은 구분한다.

## 확정된 C++ 사용법

전용 소스 변환기를 만들지 않고 표준 C++20 매크로·등록 함수를 사용한다. 원래의 괄호 없는 데이터 키워드와 대괄호 명령 attribute는 이 결정으로 대체한다.

```cpp
#include "Engine/Data/DataRoutingMacros.h"

struct GameData {
    Rice::Data::DataContext& context;
    RICE_GAME_FIELD(context, int, round, 1);
    RICE_SLOT_FIELD_ID(context, int, score, "game.score.v1", 0);
    RICE_SLOT_FIELD(context, glm::vec3, position, glm::vec3(0));
    RICE_SLOT_FIELD(context, std::vector<int>, items, std::vector<int>{});
    RICE_GLOBAL_FIELD(context, int, total, 0);

    explicit GameData(Rice::Data::DataContext& ctx) : context(ctx) {
        score.Description("슬롯에 유지되는 점수");
    }
};

// 런타임이 바인딩된 context로 생성한 뒤:
// data.score += 10;
// data.position.x = 2.0f;
// data.items.push_back(3);
```

필드의 읽기·쓰기는 프록시가 관찰한다. 사용자 구조체는 RICE_DATA_STRUCT로 멤버를 등록한다. 원시 mutable 참조·포인터·iterator는 노출하지 않는다. 타입을 정확히 요구하는 외부 템플릿 함수에는 Read() 스냅샷을 넘긴다. 명령은 NativeCommand의 함수 포인터 등록과 공유 인자 목록 매크로를 사용하며, 패턴 문자열의 이름·매핑·접미사 규칙은 유지한다.

## 구현된 기반

| 영역 | 구현 | 실제 연결 경계 |
|---|---|---|
| 저장 | Windows SQLite, WAL/FULL, 타입별 Codec, CAS, 슬롯 CRUD, 게임 메모리 수명, global 최초 읽기/쓰기 재조정 | global 재조정은 별도 로컬 DB와 권위 DB 어댑터에서 검증. Controller IPC 연결은 이후 |
| 데이터 표기 | scalar 연산, Vector 멤버, 배열/목록 및 등록 구조체 프록시, native 선언 매크로, 오류 조합 static_assert | 게임별 데이터 객체는 DataContext에 명시적으로 바인딩 |
| 슬롯 | 안정적 ID, 같은 인스턴스 공유, 메인 지정, 최초/마지막 활성 전환, 대상 소유권 검사, 설정 직렬화 | 미디어 백엔드가 실제 연결 상태를 보고할 때 활성화. UI 버튼으로 가짜 Live 상태를 만들지 않음 |
| Controller | 방송 슬롯 탭, 관리 화면, 1080p60 기본 프로필, 코덱·지연·Pause 설정, 비동기 SQLite 저장·데이터 편집 | 설정 저장 기능. 실제 인코더가 설정을 적용하는 단계는 이후 |
| 사이클 | monotonic clock, 차단 시간 제외, 다음 사이클 revision, response 우선순위, 취소·Pause, 목록 연산 | Player 입력·출력 수명과의 최종 연결은 이후 |
| Editor | 좌우 명령/함수 연결, Inspector 권한, 시각적 사이클 타임라인, 필드별 override, 프로젝트 JSON 저장·복원 | 로컬 모의 테스트는 현재 씬의 실제 함수를 호출하며 게임 데이터를 변경할 수 있음. 패턴 검사만 수행하는 기능은 별도 |
| 명령 | 패턴 파서, named 인자, native 타입 연결, 순차 다중 매칭, Listener, 수신 큐와 객체 수명 검증 | 미설정 권한 평가기는 원격 명령을 거부. Controller와의 채팅 전달 IPC는 이후 |
| API | HTTPS videos/list 및 liveChat/list client, polling 제한·세대 취소·키 장애 전환, 보호된 작성자 정보, 권한 정책 | 공급받은 자격 증명으로 요청할 수 있는 client. 실제 계정 로그인·UI 연결은 이후 |
| 비밀정보 | Windows 현재 사용자 DPAPI 암호화, 참조 저장, 로그 redaction | 실제 사용자 키를 자동으로 읽거나 계정을 연결하지 않음 |

## 남은 통합

1. Controller와 Player의 데이터/채팅/사이클 프로토콜 연결, 저장 결과의 재시도 및 UnknownCommit 복구.
2. Controller 재시작 시 살아 있는 Player 재연결, global 로컬 fallback의 실제 프로세스 수명 연결. 현재 프로세스 Job·파이프 오류 정책을 이 단계에서 함께 변경해야 한다.
3. OAuth 브라우저 로그인·토큰 갱신, 사용자 키 관리 UI, Gladius 파일 가져오기와 실제 방송 연결.
4. Controller 송출 시작 UI와 자격 증명·채팅 전달 IPC의 최종 연결, 목적지별 재연결, Pause 합성. Player Source/오디오 추출과 실제 미디어 교체 경로는 아래와 같이 구현·로컬 검증했다. 실제 송출 시작 버튼은 아직 활성화하지 않았다.
5. 이 PC의 5개 1080p60 송출 및 표시 토글 부하 시험. 모델 테스트를 이 성능 목표의 통과로 간주하지 않는다.

## 검증 결과

2026-11-16 Windows MSVC Release에서 Editor·Player·Controller를 빌드했다. 신규 기능과 영향받는 기존 동작을 대상으로 선택한 CTest **21개가 모두 통과**했다. 저장·native 접근·컴파일 오류 계약·사이클·패턴·Listener·설정 저장·실제 ImGui 패널 생성·모의 API·Controller 프로세스 제어를 포함한다. 기존 Workspace/Prefab/Scene/Inspector/EditorHistory 회귀도 함께 통과했다.

Controller 파이프와 컴파일 실패 검사는 샌드박스의 named pipe/SDK 레지스트리 제한 때문에 일반 사용자 권한에서 검증했다. 실제 사용자 YouTube 계정 접속, 실제 방송 시작, 5개 출력 성능 시험은 실행하지 않았다.

빌드 디렉터리는 Build/logging이며, CMake의 Release 산출물을 저장소 루트의 Editor·Player·Controller 실행 파일로 설치했다. 검증 중에는 별도의 TestData/임시 디렉터리와 합성 자격 증명만 사용했다.

## 송출 백엔드 결정

2026-11-16 사용자 결정: **FFmpeg 기반 별도 미디어 어댑터**를 구현하고 OBS의 Source/Encoder/Output 구조를 참고한다. libobs는 직접 결합하지 않는다. FFmpeg 의존성·미디어 어댑터·Player Source를 연결했으며, Controller 시작 버튼의 연결은 아직 남아 있다.

- **OBS 구조 참고 + 별도 미디어 어댑터**: FFmpeg의 적절한 LGPL 구성과 하드웨어 인코더를 이용해 Rice의 OpenGL 렌더·오디오 경로에 연결한다. 구성·라이선스 고지·배포할 라이브러리 소스 제공 조건을 관리한다. [FFmpeg 라이선스 안내](https://ffmpeg.org/legal.html)
- **libobs 직접 사용**: OBS의 합성·인코더·Output을 직접 활용하고 Rice Source를 연결한다. OBS의 GPL 조건을 포함해 Rice와 결합한 프로그램의 배포 방식을 결정해야 한다. [OBS 라이선스](https://github.com/obsproject/obs-studio/blob/master/COPYING)

선택한 첫 번째 방식에서는 FFmpeg DLL의 LGPL 구성을 유지하며 게임 Source, 인코더, 목적지별 Output을 분리한다. 인코더는 기준 PC의 NVIDIA 하드웨어를 우선 사용한다. 실제 5개 출력 성능은 별도 시험한다.

사용자 확정: **하드웨어 인코더 초기화 실패·지원 불가·동시 세션 한도 도달 시 신규 슬롯 시작만 거부한다.** 기존 슬롯의 인코더·출력·설정은 변경하지 않는다. CPU 인코더 전환, 해상도·FPS·비트레이트 자동 하향은 하지 않는다. `auto`는 현재 지원하는 해당 코덱의 NVIDIA 하드웨어 인코더 선택을 의미하며 소프트웨어 fallback을 뜻하지 않는다.

같은 런타임 인스턴스와 같은 영상 인코딩 설정을 쓰는 슬롯은 인코더를 공유한다. 목적지·YouTube 지연 설정은 인코더 공유 여부에 영향을 주지 않는다. 다른 인스턴스 또는 다른 인코딩 설정은 별도 세션이 필요하며, 생성에 실패하면 기존 세션을 유지한 채 새 요청을 거부한다. 공유 중인 슬롯 하나의 중지는 다른 슬롯의 인코더를 해제하지 않는다. 출력은 최대 5개로 제한한다.

미디어 어댑터는 `VideoEncoder`의 RGBA→BT.709 NV12→NVENC 패킷 생성, `EncoderSessions`의 원자적 세션 추가·공유, `EncodedOutput`의 AAC 및 FLV/RTMPS 결합을 제공한다. `PlayerOutput`은 목적지별 작업 스레드·유한 소스 변경 큐·공통 출력 시계와 Player Source IPC를 연결한다. 현재 PlayerOutput은 출력별 인코더를 소유하므로 EncoderSessions의 공유 기능과 최종 결합은 남아 있다. 재연결, 추가 송출 지연, Pause 오버레이, 새 목적지의 공유 인코더 IDR 대기는 미완료다. 추가 송출 지연이 0이 아니면 시작을 거부한다. 인코더 초기화나 헤더 기록만으로 시작 성공을 알리지 않고 첫 영상 패킷 기록을 기다린다.

빌드 구성과 GPU 검증 구분은 [FFmpegBuild](FFmpegBuild.md)에 기록한다.

추가 사용자 확정: 게임 교체 준비 중에는 **슬롯에서 지정한 대기 이미지 + 무음**을 송출한다. 슬롯 설정 저장·복원과 관리 입력란, PNG/JPEG/BMP 로딩 및 종횡비 유지 RGBA 프레임 생성기를 추가했다. 이미지가 없거나 손상되면 교체 시작 전에 실패하며 기존 출력은 유지한다. PlayerOutput에 실제 Source 교체와 무음 공급을 연결했다.

추가 사용자 확정: 교체 완료 후 기존 게임을 사용하는 출력 슬롯이 없으면 저장 후 자동 종료한다. `ReplacementLifecycle`은 교체 완료 시 마지막 사용 여부를 검사하고, 명시적으로 공급받은 비동기 저장 완료 응답 이후에만 `ProcessManager::StopIfCurrent`로 정상 종료를 요청한다. 다른 슬롯의 송출·재연결·초기 연결, 저장 실패, 오래된 실행 ID는 종료를 막는다. 저장 실패는 오류를 남기고 명시적 재시도를 지원하며 자동 강제 종료하지 않는다.

이 수명 관리기는 `ReplacementLifecycle(ProcessManager&)`를 통해 실제 Player의 `SaveForRetirement` IPC와 연결된다. Player는 게임 종료 콜백을 실행하고 게임·렌더·게임 UI 콜백을 정지한다. 프로세스 내 Data Routing SQLite 트랜잭션이 모두 FULL 동기 커밋/롤백을 마칠 때까지 비차단 방식으로 확인한 뒤, 이후 트랜잭션 진입을 차단하고 `durable-and-sealed` 응답을 보낸다. Controller는 응답의 요청 ID·실행 ID·명령·완료 토큰을 검증한 후에만 저장 future를 완료한다. 그 이후 정상 `Stop`을 보내며 자동 강제 종료하지 않는다.

지원 capability가 없는 이전 Player, 파이프 단절, 응답 시간 초과, 저장 응답 이전 프로세스 종료는 저장 실패로 처리한다. 기존 `Stop` 응답이나 Controller 설정 저장으로 Player 데이터 저장 완료를 대신하지 않는다. 봉인된 Player는 Resume을 거부한다. 이 봉인은 한 인스턴스 전용 Player 프로세스에만 적용하고 Controller에는 적용하지 않는다. 현재 Data Routing은 쓰기마다 동기 커밋하며, 향후 별도 비동기 원격 쓰기 큐를 추가하면 해당 큐의 ACK를 이 저장 장벽 앞에 연결해야 한다.

**실제 Player 미디어 Source 교체와 저장 완료 IPC는 연결했다. Controller UI 시작 경로는 아직 남아 있다.** 모든 게임은 `Rice::Streaming::ReadyForStream()`을 명시적으로 호출해야 한다. 비동기 작업에서는 시작 시 `StreamReadiness::Token()`을 캡처하고 해당 토큰으로 신호를 보낸다. 이전 실행의 늦은 신호는 거부한다. 정상 실행 중 준비 신호가 없으면 제한 없이 대기하고 수동 취소만 허용한다.

`GameReplacementFlow`는 대기 이미지 기록 ACK → 숨김 Player 시작 → `RestoreSlot` ACK·채팅 준비·스크립트 신호·첫 프레임 → 실제 Source 전환 ACK → 슬롯 배정 확정 → 이전 Player 저장/종료 순서다. `RestoreSlot`은 살아 있는 DataContext의 선언 필드를 검증·생성한 뒤 메인 슬롯을 재연결한다. 취소·실패 시 이전 게임의 현재 Source로 복귀한다. 대기 중 기존 게임의 Update는 계속 수행하며 해당 슬롯 입력만 차단한다.

`SlotChatSession`은 실제 YouTubeClient의 직렬 조회 작업을 소유한다. 최초 이력, 차단 중 입력, 이전 입력 세대의 늦은 응답, 복귀 뒤 받은 과거 메시지를 버린다. 게임 수신기의 `SetSlotMuted`는 해당 슬롯의 대기 큐·분리된 배치도 무효화하고 다른 슬롯에는 영향을 주지 않는다. `ConnectReplacementServices`는 실제 미디어·복원 IPC·채팅 준비를 결합하며, 이미 전달된 명령을 무효화할 콜백을 필수로 요구한다. Controller→Player 채팅/사이클 전달 IPC와 UI 최종 조립은 미완료이므로 이 연결 함수만으로 실제 채팅 명령 실행이 완성된 것은 아니다.

Player는 1920×1080 오프스크린 렌더와 게임 UI를 삼중 PBO로 읽어 공유 메모리 최신 프레임에 게시한다. FMOD master mix는 48kHz 스테레오 float 링 버퍼로 전달한다. PlayerOutput은 독립 시계로 NVENC/AAC 패킷을 기록하고 대기 이미지는 무음으로 처리한다. 숨김/최소화 시 미리보기 blit을 생략한다. 현재 원본은 고정 1080p이며 다른 출력 해상도는 스케일링한다. 미리보기 창 이벤트·VSync가 렌더 생산에 미치는 영향과 5개 출력 지속 성능은 추가 검증 대상이다.

사용자 확정: 교체 취소 시 준비 중인 새 게임의 slot 변경을 저장하고 정상 종료한다. `RetireUnused` → `SaveForRetirement` ACK → `StopIfCurrent` 경로를 사용하며 종료 콜백의 쓰기도 포함한다. 다음 시도는 저장값을 이어 사용하고 롤백하지 않는다. 다른 슬롯이 사용하는 공유 인스턴스는 유지하며 저장 실패는 재시도 대상으로 남긴다. 실제 Player 통합 테스트는 준비/종료마다 값을 증가시켜 취소 후 2, 재시도 시작 후 3, 재취소 후 4, 최종 교체·종료 후 6이 보존되는지 검사한다.

2026-11-16 재검증: 위 취소·재시도 시나리오를 포함한 `ControllerIntegration`과 실제 NVENC/로컬 FLV의 `PlayerMediaIntegration`이 모두 통과했다. 종료된 이전 실행의 retiring 상태가 새 실행을 막던 검사도 수정했다. 저장/종료 중인 살아 있는 실행의 재사용은 계속 거부한다.

로컬 검증: `DataSealWrite/Read`에서 진행 중 쓰기 대기·봉인 이후 쓰기 거부·다른 프로세스 재읽기를 확인했다. `SlotChatSession`은 모의 HTTP로 늦은 응답과 과거 명령 차단을 확인했다. `ControllerIntegration` 및 `PlayerMediaIntegration`은 실제 Player에서 준비 대기·수동 취소·스크립트 준비·RestoreSlot·종료 콜백의 SQLite 변경·저장 ACK·정상 종료를 검증했다. 후자는 실제 NVENC/AAC로 기존 게임→대기 이미지→새 게임을 로컬 FLV에 기록한다. 채팅은 모의 응답이며 실제 YouTube 접속과 5개 동시 송출 성능은 검증하지 않았다.

수명 관리 추가 검증: `ReplacementLifecycle`에서 공유 인스턴스 유지, 마지막 슬롯 교체 후 저장 대기, 저장 실패·재시도, 오래된 응답, 교체 거절의 원자성, 다른 슬롯의 초기 연결 중 종료 금지를 검증했다. `ControllerIntegration`은 실제 로컬 Player에 잘못된 실행 ID로 종료를 요청해 거부되는지와 올바른 ID의 정상 종료를 검증했다. 두 테스트 모두 통과했다.

### FFmpeg 단계 검증 (2026-11-16)

FFmpeg 8.1.1의 LGPL 2.1+ 공유 라이브러리를 빌드하고 Controller Release에 연결했다. 추가·영향 대상 CTest 6개(`EncoderSessions`, `WaitingImage`, `HardwareEncoder`, `StreamingSlots`, `StreamingPanel`, `ControllerWindow`)가 통과했다. 한글 이미지 경로 문제는 명시적 UTF-8→Windows 경로 변환으로 수정 후 재검증했다.

이 PC에서 H.264/HEVC/AV1 각각 1080p 합성 프레임 12개를 실제 NVENC로 인코딩하고 디코딩했다. AAC와 결합한 로컬 FLV 파일을 다시 열어 스트림·패킷 수를 검사했다. 독립 H.264 NVENC 세션 5개를 동시에 열어 각 세션의 프레임 생성을 확인했다. **짧은 기능 검사이며, 5개 동시 1080p60 지속 성능 또는 실제 YouTube RTMPS 접속 성공을 의미하지 않는다.** 실제 방송·계정 접속은 실행하지 않았다.
