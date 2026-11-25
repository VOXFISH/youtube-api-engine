# YouTube 연동 및 스트리밍 명세서

갱신일: 2026-11-16  
대상: Windows x64, Rice Editor, Rice Player, Rice Controller  
상태: 사용자 결정 반영, 구현과 수용 시험의 기준

구현된 코드·화면과 아직 연결되지 않은 실행 경계는 [스트리밍 구현 현황](StreamingImplementation.md)을 따른다.

## 1. 범위

Gladius의 YouTube 설정·채팅 수신·명령 파싱·권한·API 키 관리를 게임 규칙과 분리해 Rice에 구현한다. Controller의 방송 항목을 **슬롯**이라 부르며, 슬롯은 게임 인스턴스·방송 설정·SQLite 데이터를 보관한다. Redis/PostgreSQL은 필요하지 않다.

- [Data Routing](../../Engine/Data/DataRouting.md): 데이터 선언, stream→slot 변경, 저장·복구·외부 쓰기 권한.
- [사이클과 YouTube 명령](CycleAndCommands.md): 사이클 시계, 타임라인, 콜백, 명령 문법과 Listener.
- 이 문서: 슬롯 운영, 방송 설정, 인증·채팅, 미디어·창 동작과 수용 시험.

키 교체 대상은 **YouTube Data API 키**다. 현재 정상 키를 계속 쓰고 키별 장애 발생 시에만 교체한다. 송출용 스트림 키 자동 순환과 정상 요청마다의 RoundRobin은 제공하지 않는다. OAuth, API 키, 송출 키를 구분한다.

일반 채팅 로그 UI·장기 보관, 유료 원격 변수 API, Gladius 전투·신전·보상·AI 로직은 이후 범위다. 명령 결과와 장애 진단에 필요한 최소 기록은 구현한다.

## 2. 슬롯과 인스턴스

### 2.1 식별자와 연결

| 식별자 | 의미 |
|---|---|
| SlotId | Controller 방송 항목의 영구 ID. 이름·게임 변경과 재시작에도 유지 |
| InstanceTypeId / BuildId | 데이터 호환 게임 종류의 안정적인 ID. 실행 파일 경로나 매 빌드 해시와 분리 |
| InstanceId | 한 번 실행한 게임 프로세스. 같은 게임을 두 번 실행하면 서로 다름 |
| MainSlotId | 해당 인스턴스의 slot 데이터 기준 슬롯 |
| OutputSessionId | 이번 송출 수명. 일시적 재연결 중에는 유지 |
| BindingGeneration | 슬롯과 인스턴스 연결 세대. 이전 응답·명령 배제 |
| BroadcastId / LiveChatId | YouTube 방송·채팅 자원 ID. SlotId와 구분 |

한 슬롯은 동시에 한 게임 인스턴스만 송출한다. 한 인스턴스를 여러 슬롯에 배정하면 같은 게임 화면·오디오를 여러 목적지로 보내며 게임을 복제하지 않는다. 각 슬롯의 입출력은 동시에 작동하고 **사이클은 인스턴스당 하나**다. 다른 방송에서 같은 텍스트를 입력한 것은 별개 명령이다. 같은 LiveChatId+messageId를 중복 전달한 경우만 제거한다.

동일 방송·채팅 자원을 서로 다른 인스턴스가 동시에 소유하지 못하도록 lease를 둔다. 동일 인스턴스에 중복된 채팅은 한 번만 구독한다. 같은 ingest 목적지에 두 Output을 연결하려는 설정도 거부한다.

### 2.2 메인 슬롯

처음 인스턴스를 지정한 슬롯을 메인으로 자동 지정하고 송출 전에 확인한다. 사용자는 변경할 수 있다. 모든 슬롯에서 들어온 명령과 게임 함수는 인스턴스의 **메인 슬롯 데이터**를 쓴다. 입력 출처에 따라 데이터 기준을 임시로 바꾸지 않는다.

메인 변경은 안전한 실행 경계에서 이전값 commit → 새 슬롯 snapshot 검증 → 전체 rebind 순으로 처리한다. 없는 선언 필드는 default를 사용하며 실패 시 이전 연결을 유지한다. 메인 슬롯의 출력만 중지하면 메인 지정은 유지한다. 다른 활성 슬롯이 있으면 인스턴스의 사이클·송출 수명도 유지한다.

메인 슬롯 자체를 삭제하거나 다른 인스턴스로 교체할 때 기존 인스턴스가 다른 슬롯에서 계속 실행된다면, 기존 인스턴스의 새 메인을 먼저 지정한다. 숨은 자동 변경으로 데이터 기준을 바꾸지 않는다.

### 2.3 게임 교체와 이벤트

교체 완료 후 기존 인스턴스를 사용 중인 출력 슬롯이 하나도 남지 않으면, **저장을 완료한 뒤 기존 게임을 자동 종료한다(사용자 확정).** 새 게임 준비·전환 실패 시 기존 게임을 종료하지 않는다. 다른 슬롯이 송출·재연결·초기 연결 중이라면 해당 인스턴스를 유지한다. 종료 전 기존 인스턴스의 입력·게임 쓰기를 정지하고 종료 관련 데이터 쓰기까지 SQLite commit 및 IPC 저장 확인을 끝낸다. Controller 설정 저장만으로 Player 데이터 저장 완료를 대신하지 않는다. 저장 실패·응답 불명확 시 자동 강제 종료하지 않고 오류와 재시도 상태를 남긴다. 저장 응답과 종료 요청은 entry ID뿐 아니라 runtime instance ID로 검증해 재실행된 게임을 종료하지 않는다. 정상 종료 시 game 데이터 수명은 끝나고 slot/stored/global 영속 값은 보존된다.

슬롯에서 X→Y→X로 게임을 교체해도 X의 저장값은 보존한다. 같은 슬롯이 다시 X의 메인이 되면 X의 값을 복원한다. 게임 종류가 달라 이름이 같은 변수도 섞이지 않는다.

교체 대기 중 기존 게임은 계속 실행한다. 대기 중인 슬롯의 명령만 버리고 다른 슬롯의 입력에는 영향을 주지 않는다. 대기 전에 시작된 조회의 늦은 응답·이미 큐에 들어온 명령·복귀 뒤 도착한 대기 구간 메시지도 실행하지 않는다. 취소하면 기존 게임의 현재 화면으로 복귀하며 버린 명령을 재생하지 않는다.

새 게임은 슬롯 데이터 복원·채팅 연결·첫 프레임 준비와 함께 스크립트의 명시적인 `Rice::Streaming::ReadyForStream()` 신호가 있어야 전환한다. 첫 프레임만으로 준비 완료를 추정하지 않는다. 정상 실행 중 신호가 오지 않으면 시간 제한 없이 대기 이미지와 무음을 송출하며 수동 취소만 허용한다. 프로세스 종료·IPC 실패는 별도 오류다.

교체 취소 시 새 게임이 준비 중 변경한 slot 데이터는 보존한다. 해당 교체를 위해 시작한 새 게임은 종료 콜백의 변경까지 저장 완료 응답을 확인한 뒤 정상 종료한다. 다음 시도는 같은 게임·메인 슬롯의 저장값을 이어 사용한다. 교체 직전 값으로 롤백하지 않는다. 다른 슬롯이 사용 중인 공유 인스턴스는 종료하지 않으며, 저장 실패 시 자동 강제 종료하지 않고 오류와 재시도를 제공한다.

송출 중 교체는 슬롯 Output과 시계를 유지하고 입력 Source만 바꾼다. **사용자 확정: 준비 중에는 Controller에서 지정한 대기 이미지와 무음을 송출한다.** 새 게임 준비·데이터 복원·채팅 연결 후 프레임 경계에서 전환한다. 실패하면 기존 Source로 복귀한다. 두 게임 오디오를 동시에 섞지 않는다. 대기 이미지는 슬롯 관리에서 지정하며, 미지정 또는 읽기 실패는 교체 시작 전에 거부하여 기존 송출을 보존한다. 이미지의 종횡비를 유지하고 남는 영역은 검정으로 채운다. 기존 인스턴스를 공유하는 다른 슬롯은 계속 송출하며, 한 슬롯의 교체를 이유로 공유 인스턴스를 전역 Pause하거나 종료하지 않는다.

- 이전 인스턴스에 다른 활성 슬롯이 남으면 기존 사이클을 유지한다. 마지막 슬롯이면 OnStreamEnd와 사이클 종료를 처리한다.
- 새 인스턴스의 첫 송출이면 OnStreamStart를 한 번 호출한다. 이미 실행 중인 인스턴스면 새 입력은 다음 사이클부터 합친다.
- 게임용 OnStreamStart/End는 활성 슬롯 수 0→1 / 1→0 기준이다. 슬롯별 연결 상태 이벤트와 분리한다.
- 재연결 중인 슬롯은 명시적 중지 또는 최종 실패 전까지 활성 집합에 유지한다. 네트워크 재시도로 Start/End를 반복하지 않는다.

## 3. 설정과 데이터 관리 화면

Controller의 슬롯마다 인스턴스 선택·공유, 메인 지정, 송출 시작/중지, 미리보기, **관리** 버튼을 제공한다. 관리는 방송 설정과 데이터 관리 탭을 연다.

| 방송 설정 | 항목 |
|---|---|
| 연결 | OAuth 채널, VideoId/URL, 방송 선택, 채팅 상태, API 키 프로필, 송출 주소·키 참조 |
| 영상 | 해상도, FPS, 비트레이트, 코덱, 지원되는 인코더, 키프레임 간격 |
| 지연 | YouTube 일반/짧은/매우 짧은 지연, 선택적 추가 송출 지연(초) |
| 연결 유지 | 재연결 간격·횟수, 신호 중단 시 YouTube 자동 종료 |
| Pause | 정지한 게임 화면, PNG 오버레이·불투명도, 송출 중지 여부 |
| 사이클 | 현재값·다음 적용값·설정 출처. 같은 인스턴스 슬롯에는 같은 값 표시 |
| 진단 | 출력 FPS·드롭·큐·A/V 차이·재연결·API 오류 |

데이터 관리에서는 게임 종류별 이름·설명·타입·값·default를 표시하며 이름/설명/값 편집, 추가, 삭제, 모두 초기화를 제공한다. **locked도 로컬 운영자가 수정 가능**하다. 타입 오류는 저장 전에 거부한다. 선언 필드 삭제는 현재 실행값도 default로 되돌리고 저장 override를 제거한다. 미연결 수동 추가 데이터는 보관하되 임의의 C++ 필드에 연결하지 않는다. 전체 초기화는 선택한 게임/슬롯 전체 중 범위를 표시하며 다른 슬롯·global을 지우지 않는다.

Editor **설정 → Streaming → YouTube Commands**는 왼쪽 명령, 오른쪽 대상 객체·YouTube Listener·함수·인자·권한·코드 미리보기다. Inspector에서도 등록한다. 함수 생성은 소스 저장·재빌드를 거친다.

**창 → 설정 → 사이클**은 PowerPoint 애니메이션과 같은 별도 타임라인 창이다. Inspector와 같은 모델로 순서·기준·시간·차단·종료·무응답 옵션을 편집한다. 스크립트 기본값을 적용한 뒤 Inspector 수정이 우선한다. cycleTime은 Editor/Controller/스크립트에서 변경하며 다음 사이클부터 적용한다.

프로젝트 기본값·명령·사이클은 Project/Streaming의 버전 있는 JSON과 build manifest로 Export한다. 로컬 슬롯·프로필·override·데이터는 SQLite에 저장한다. API 키·OAuth refresh token·송출 키는 Windows 사용자 범위 비밀 저장소를 사용하며 DB에는 secretRef만 보관한다. 프로젝트·Export·로그·에러 URL에 비밀을 넣지 않는다. 검증된 설정 revision을 원자적으로 반영하며 재시작 필요 값은 현재/예약 값을 구분한다.

2026-11-16 추가 결정에 따라 스크립트는 **표준 C++와 괄호 있는 매크로·등록 함수**를 사용한다. 비표준 대괄호 attribute나 키워드 접두어를 해석하는 전용 소스 생성기는 만들지 않는다. 영속 변수는 native 프록시를 통해 배열·목록 함수와 등록된 구조체 멤버의 일반적인 접근 표기를 제공하고, SQL 저장을 우회하는 원시 포인터·참조는 노출하지 않는다.

## 4. 방송 프로필과 OBS 참고

| 항목 | 기본값과 계약 |
|---|---|
| 영상 크기 | **1920×1080 / 60 FPS**, Controller에서 변경 가능 |
| 영상 인코딩 | H.264, CBR 17 Mbps, 키프레임 2초 |
| 코덱 | H.264 / HEVC(H.265) / AV1 선택. 실제 장치·인코더·전송 포맷 지원 검사 |
| 인코더 | 호환 하드웨어 인코더 우선. 시작 전 장치·동시 세션 검증 |
| 오디오 | 게임 믹스만, AAC 48 kHz stereo 128 kbps. 시스템 전체·마이크 캡처 제외 |
| YouTube 지연 | Low 기본안. Normal / Low / UltraLow |
| 추가 송출 지연 | 0초. 유한 버퍼로 구현하고 다음 Output 시작부터 적용 |
| Pause | 송출 유지, 마지막 게임 화면, 오버레이 없음, 게임 오디오 무음 기본안 |
| 신호 중단 자동 종료 | 끔 기본안. 실제 YouTube 적용 여부를 별도 표시 |

2026-11-16 확인한 YouTube 1080p60 권장 비트레이트는 H.264 17 Mbps, HEVC·AV1 12 Mbps다. 사용자가 변경할 수 있으며 시작 시 지원 조건을 검사한다. [YouTube 인코더 설정](https://support.google.com/youtube/answer/2853702?hl=en)

YouTube 시청자 지연은 latencyPreference의 normal/low/ultraLow다. OBS식 추가 송출 지연 버퍼와 별개이며 monitorStream.broadcastStreamDelayMs로 대신하지 않는다. OAuth가 없는 연결에서는 적용 방법을 안내하고 미확인 상태로 표시한다. [YouTube 방송 속성](https://developers.google.com/youtube/data/live/reference/rest/v3/liveBroadcasts)

OBS의 Source/Scene → Encoder → Output 분리와 별도 Display를 참고한다. 동일 Source와 동일 영상·오디오 프로필은 인코딩 패킷을 공유할 수 있다. 각 목적지는 독립 큐·연결·재시도를 가진다. 오버레이·해상도·코덱이 다르면 필요한 합성·인코딩을 분리한다. 이는 Rice의 다중 슬롯 구현이며 OBS 기본 UI가 곧 5개 독립 방송을 제공한다는 뜻은 아니다. [OBS 백엔드](https://docs.obsproject.com/backend-design), [복수 Output 인코더 구현](https://raw.githubusercontent.com/obsproject/obs-studio/master/libobs/obs-encoder.c)

Pause는 Output 데이터 전달을 끊는 API로 처리하지 않는다. 마지막 게임 texture를 연속 PTS와 정해진 FPS로 계속 보내고 PNG alpha×설정 opacity로 합성한다. 미리보기만 중지할 때에는 Display/Swapchain만 중지·회수한다. [OBS Output](https://docs.obsproject.com/reference-outputs), [이미지 소스](https://obsproject.com/kb/image-sources), [Display 구현](https://raw.githubusercontent.com/obsproject/obs-studio/master/libobs/obs-display.c)

OBS 참고와 libobs 직접 결합은 별도 결정이다. 미디어 백엔드를 어댑터로 분리하며 실제 라이브러리·배포 방식을 선택할 때 결정 기록을 남긴다. OBS 소스 재사용과 FFmpeg 빌드 구성은 각각 라이선스 조건을 확인한다. [OBS 저장소](https://github.com/obsproject/obs-studio), [FFmpeg 라이선스](https://ffmpeg.org/legal.html)

## 5. 프로세스와 미디어 경로

```text
Controller: 슬롯 / 인증 / ChatClient / 배치 제어 / SQLite 서비스
    ↕ 버전 있는 명령·데이터 IPC
Player: 게임 / Listener / 행동 실행 / Routed 접근자
    ├─ 고정 송출 타깃 + 게임 오디오 → 미디어 전용 채널
    └─ 완성 프레임 → 선택적 로컬 미리보기

Controller 수명에 종속된 MediaService:
    Player Source → 슬롯 합성 → Encoder → 목적지별 RTMPS Output
```

Controller는 Output 수명을 소유한다. MediaService가 별도 worker여도 Controller 연결·lease 상실 시 송출을 종료한다. Player는 게임·렌더 Source·메인 스레드 호출을 소유하며 독립적으로 생존 가능하다. raw 1080p60 프레임은 JSON 제어 파이프가 아닌 GPU 공유 또는 제한된 공유 메모리 풀로 전달한다.

첫 경로는 H.264+AAC+RTMPS다. HEVC/AV1은 전송 포맷 확장을 지원하는 backend에서 활성화한다. UI 선택만으로 미지원 코덱을 성공 처리하지 않는다. [YouTube RTMPS](https://developers.google.com/youtube/data/live/guides/rtmps-ingestion)

원본은 창 캡처가 아닌 고정 FBO다. 창 크기·최소화·0 크기 framebuffer가 방송 해상도·aspect·FPS에 영향을 주면 안 된다. 숨겨진 호스트 컨텍스트와 별도 미리보기를 사용한다. [GLFW 컨텍스트](https://www.glfw.org/docs/latest/context.html)

게임 상태는 불변 RenderSnapshot으로 전달하고 GPU 생성·업로드·삭제도 렌더 스레드 작업 큐로 옮긴다. 현재 Workspace::Update의 GPU 업로드, Scene/Component 직접 렌더, resize의 glViewport 변경을 함께 분리한다. 마지막 GPU 사용 fence 뒤에 해제하며 게임 객체를 두 스레드가 동시에 수정하지 않는다.

영상 큐 기본 3프레임, 미리보기는 최신 프레임 하나다. 정체 시 미인코딩 원시 프레임을 먼저 버린다. 압축 참조 프레임을 임의로 삭제하지 않는다. 목적지 정체가 지속되면 다음 IDR부터 복구한다. CPU readback은 PBO/fence로 비동기화하며 zero-copy 경로와 비용을 구분한다. 오디오 샘플 시계와 monotonic PTS는 창 복원·게임 교체에도 역행하지 않는다.

우선순위는 송출 타임라인·오디오·프레임 유지 → 게임 작업 예산 관리 → 창 생성·복원·미리보기다. UI/Swap/VSync가 송출 락을 쥐지 않게 하고 미리보기가 프레임을 붙잡지 않게 한다. 과부하에서는 미리보기 복원을 늦출 수 있다.

## 6. 창과 종료 계약

| 조작 | 동작 |
|---|---|
| 최소화 / 숨김 / 관리 Player 최소화 버튼 | 미리보기만 중지, 게임·송출·채팅 유지 |
| Controller 관리 Player X | 미리보기 닫기 기본안. 네모 종료 버튼과 구분 |
| 관리 Player Pause | 게임 Pause, 채팅 유지, 기본은 정지 화면 송출과 선택 PNG |
| Pause시 송출 중지 설정 | 해당 인스턴스 활성 Output 중지. Resume 재개 시 종료된 YouTube 방송을 자동 생성하지 않음 |
| Resume | Pause 구간 명령 폐기, 몰아서 실행 금지 |
| 슬롯의 빨간 원 다시 누르기 | 해당 슬롯 송출 중지 |
| 공통 Player 빨간 원 다시 누르기 | 해당 인스턴스의 모든 슬롯 송출 중지로 표시 |
| Player 네모 | 인스턴스 전체 송출 중지·저장 후 게임 종료 |
| 독립 Player X | 일반 게임 종료 |
| Controller 명시적 전체 종료 | 확인 후 모든 송출·저장 종료, 관리 프로세스 종료 |

확인 문구는 **모든 방송이 종료됩니다. Rice Controller를 종료하시겠습니까?**, 버튼은 **종료 / 취소**다. 새 입력 중지→사이클 취소→출력 종료→데이터 flush→프로세스 종료 순서를 지킨다. 저장 실패를 성공으로 표시하지 않는다.

Controller 비정상 종료 시 신호는 끊긴다. 살아 있는 Player의 global은 로컬 SQLite 임시 저장으로 전환한다. Controller 복귀 후 키별 첫 쓰기이면 로컬값으로 global을 갱신하고 첫 읽기이면 global로 로컬을 갱신한다. 자동 선행 동기화는 하지 않는다. 상세 계약은 Data Routing을 따른다.

신호 중단과 YouTube 방송 이벤트 종료는 별개다. enableAutoStop=false로 신호 상실 자동 종료를 끌 수 있지만 영상 공급이 유지되는 것은 아니다. 재연결은 살아 있는 Controller만 수행한다. 로컬 송출 중지와 YouTube Complete 전환을 구분하고 후자는 슬롯 정책에 따른다. 기본은 자동 종료 설정을 따르며 Complete된 방송에는 재연결하지 않는다. [자동 종료 설정](https://developers.google.com/youtube/data/live/reference/rest/v3/liveBroadcasts), [방송 전환](https://developers.google.com/youtube/data/live/reference/rest/v3/liveBroadcasts/transition)

## 7. Gladius 이식과 인증

기준 소스는 C:/Users/user/Documents/GitHub/Gladius/Gladius/Source/Gladius의 Backend 스크립트다. Unreal Actor/Delegate를 직접 링크하지 않고 관찰 가능한 동작을 옮긴다.

| 원본 | Rice 동작 |
|---|---|
| YouTubeChatManager | VideoId→LiveChatId, pageToken, 중복 요청 방지, timeout 15초, 오래된 응답 배제 |
| YouTubeApiKeyPool | 공백·중복 제거, 정상 키 유지, 장애 전환, 목록 재적용으로 실패 기록 초기화 금지 |
| ChatCommandParser / Dispatcher | !명령 파싱·함수 연결을 확장 패턴·레지스트리로 일반화 |
| AdminSubsystem / Commands | 검증된 channelId 관리자, 공지·로컬 차단·점검·설정 재읽기 |
| 도배 검사 | 기본 0.5초 창, 동일 내용 중복 거부, 이미 허용한 3건 이후 거부, 누적 위반×1분 제한 |
| TempleCycleController | 기본 25초, 응답 완료 후 배치 확정. 게임·AI 로직 제외, 신규 타임라인 적용 |

Gladius의 streamKey 설정 읽기를 실제 인코더·송출 구현으로 간주하지 않는다. 미디어는 신규 기능이다. 가져오기는 사용자가 선택한 파일의 youtube.apiKeys/videoId/streamKey, operators의 userId/channelId/adminName을 변환한다. 원본을 변경하지 않고 비밀정보는 보호 저장소로 옮긴다.

API 키는 허용된 공개 조회, OAuth는 계정 권한이 필요한 조회·방송 제어, 송출 키는 ingest 인증에 사용한다. OAuth는 브라우저 인증·PKCE·토큰 갱신을 사용한다. API 키만으로 모든 채팅 접근이 된다고 가정하지 않는다. [데스크톱 OAuth](https://developers.google.com/identity/protocols/oauth2/native-app)

수동 연결과 OAuth 방송 선택·제어를 지원한다. ChatOnly 개발 모드는 실제 OnStreamStart를 만들지 않는다. API 성공·ingest 연결·YouTube Live 상태를 구분한다. [방송 바인딩](https://developers.google.com/youtube/data/live/reference/rest/v3/liveBroadcasts/bind)

### API 키 장애 처리

| 결과 | 처리 |
|---|---|
| 폐기/잘못된 키, 키별 restriction 오류 | 해당 키 격리, 같은 활성 프로젝트의 다음 정상 키로 제한 재시도 |
| API 비활성화·프로젝트 설정 오류 | 프로젝트 설정 필요, 키 무한 교체 금지 |
| quotaExceeded | 프로젝트 전체 차단. 다른 프로젝트 자동 순환 없음 |
| 429 / 5xx / 일시적 네트워크 오류 | Retry-After·backoff+jitter, 정상 키 무조건 교체 금지 |
| OAuth 만료 | 토큰 갱신 1회 또는 로그인 |
| 종료 방송·채팅 비활성·잘못된 대상 | 대상 오류로 분류 |
| 사용 가능 자격 없음 | 원인·retryAt 표시, 미디어 Output은 별도 유지 |

논리 요청당 키별 최대 한 번, 전체 timeout 기본15초다. HTTP 상태와 구조화 reason으로 판정한다. 프로젝트별 quota를 키별 quota처럼 표시하지 않는다. 일일 회복은 미국 태평양 자정이며 DST를 반영한다. [정책 안내](https://developers.google.com/youtube/terms/developer-policies-guide), [quota](https://developers.google.com/youtube/data/determine_quota_cost)

## 8. 채팅과 권한

활성 채팅 조회→응답/timeout→명령 배치 확정→OnCycle→명령·행동→남은 cycleTime→다음 조회를 반복한다. list의 pollingIntervalMillis를 준수하며 streamList를 써도 같은 배치 경계를 제공한다. 짧은 cycleTime보다 API 최소 간격이 길면 다음 조회가 늦어질 수 있다. [list](https://developers.google.com/youtube/data/live/reference/rest/v3/liveChatMessages/list), [streamList](https://developers.google.com/youtube/data/live/reference/rest/v3/liveChatMessages/streamList)

OnResponse는 목록 행동보다 먼저 호출하고 차단 행동보다 우선한다. OnCycle은 배치 확정 후0번·0초 고정 행동이다. **10초 사이클에서3초 행동은 isCycleBlocked=false면7초, true면10초가 남는다.** 게임 timescale은 영향이 없다.

Pause 중 채팅 연결·토큰은 유지하지만 명령을 보관하지 않는다. Resume 후 늦게 받은 Pause 구간 메시지도 제외한다. 최초 연결 과거 채팅, 이전 세대 응답, 삭제 등 비명령 이벤트는 실행하지 않는다.

명령은 슬롯→인스턴스 전체 수신 컴포넌트→YouTube Listener→함수로 전달한다. 여러 패턴이 맞으면 위에서 아래로 모두 실행하고 지정 중복 매칭 오류를 기록한다. 이름 기반 인자·타입과 대상 생존을 검사한다.

권한은 Everyone / Owner / ModeratorOrOwner / LocalAdmin / ChannelAllowlist 및 명시적 OR다. 신규 기본은 LocalAdmin이며 공개 명령은 개발자가 Everyone으로 설정한다. 인증 응답의 channelId·owner·moderator로 판정한다. 표시 이름·입력 핸들·Verified·후원 자체는 관리자 증명이 아니다. [작성자 필드](https://developers.google.com/youtube/data/live/reference/rest/v3/liveChatMessages)

차단/비활성→허용 정책→cooldown 순서로 검사하며 실행 직전 재검사한다. 변수 locked/access/open은 외부 직접 쓰기 권한으로 명령이 허용한 내부 게임 함수 쓰기와 별개다.

운영 프리셋은 !공지, !차단, !차단해제, !점검, !점검해제, !패치다. 공지는 HUD/운영 이벤트이며 자동 YouTube 채팅 발송과 구분한다. 차단은 로컬 입력 제한이고 동명 대상이 여럿이면 거부한다. 패치는 설정 재읽기로 제한한다.

큐는 기본 인스턴스당1,000건, 원문4KiB, 유효기간2사이클이다. 초과는 QueueFull로 거부한다. 살아 있는 세션의 중복 실행을 막으며 충돌 후 임의 함수 exactly-once는 보장하지 않는다. 재시작 후 과거 명령 자동 재실행은 금지한다. 영속 데이터 성공은 SQLite commit 이후 보고한다.

## 9. 구현 위치와 단계

| 위치 | 책임 |
|---|---|
| Engine/Data | Codec, SQLite, Routed 접근, slot 복원, global 임시 DB·첫 접근 조정 |
| Engine/Streaming | CycleScheduler, CommandPattern/Registry, Listener, 이벤트, Player bridge |
| Engine/Tools/RiceCon/Source/Streaming | 슬롯·프로필·키·인증·채팅·Output 관리 |
| Engine/Tools/RiceCon/Shared | 버전 있는 설정/명령/데이터 IPC |
| Engine/Editor | Streaming 설정, 좌우 명령 연결, Inspector, 타임라인 |
| Engine/Renderer 및 Audio | 고정 타깃, snapshot·GPU 큐, 게임 믹스 |
| 스크립트 등록 계층 | 표준 C++ 매크로·typed 등록 함수, 컴파일 타입 검사, 데이터·함수 manifest |

1. 슬롯·저장·사이클·명령 파서 모델과 독립 테스트.
2. Controller 관리·데이터 편집·기존 Player IPC 연결.
3. native C++ 등록·Listener·Inspector·타임라인 연결.
4. 실제 API 인증·수신·Gladius 가져오기·권한 연결.
5. 렌더·오디오 분리, 미디어 백엔드, RTMPS·다중 출력.
6. 창·Pause·장애·성능 수용 시험. 실제 공개 방송을 자동 시험에 사용하지 않음.

각 단계에서 모델만 있는 기능과 실제 연결된 기능을 구분한다. 구현 중 사용자 선택에 따라 동작이 달라지는 미결정 사항은 해당 구현을 멈추고 질문한다.

## 10. 기준 PC와 수용 시험

2026-11-16 로컬 확인: AMD Ryzen 9 7950X(논리32), NVIDIA RTX4060Ti(표시VRAM8188MiB), 드라이버610.88, OS 조회 총 물리메모리 약63.11GiB, Windows build26200/25H2. 장치 정보이며 성능 합격 결과가 아니다.

필수 목표는 **5개 동시 방송, 각1080p60, 게임 오디오**다. 기본 H.264면 영상만5×17=85Mbps 업로드에 오디오·프로토콜·회선 여유가 필요하다. 인코딩을 공유해도 목적지별 네트워크 사용량은 별개다.

| 시험 | 통과 조건 |
|---|---|
| 한 인스턴스→5슬롯 / 독립5인스턴스→5슬롯 | 별도 시험, 각1080p60 및 GPU·인코더 세션·업로드 한계 기록 |
| 메인 중지·보조 유지 | End 미발생, 기존 메인 데이터와 사이클 유지 |
| 메인 변경·A→B→A | 정확한 슬롯값 복원, 없는 값default, 게임별 보존 |
| 데이터 CRUD·locked 편집 | 로컬 편집 허용, 타입 오류 원자적 거부, 초기화 범위 준수 |
| 게임 교체 실패·늦은 응답 | 기존 출력 유지, 이전 세대 명령 폐기, PTS역행 없음 |
| 오류별 API 키 처리 | 정상키 유지, 키 문제만 교체, 프로젝트quota 차단 |
| 복수 입력·패턴 | 출처 보존·중복 제거·순차 다중실행·오류 기록 |
| 10초/3초 행동 | 비차단7초·차단10초, 변경은 다음 사이클 |
| Pause/Resume/timescale | 기본 송출·채팅 유지, Pause 명령 재실행 없음 |
| 최소화30분·표시토글100회 | 창 때문에 encoder reset·RTMPS재접속·PTS역행 없음 |
| 창 복원 인위적 지연 | 송출은 창 락을 기다리지 않으며 미리보기만 지연 |
| 토글 전후 정상부하 비교 | 로컬 sink gap2프레임 이내, drop증가1%p 이내, A/V차100ms 이내 목표 |
| 목적지 하나 정체 | 나머지 지속, 유한 큐, 올바른 재연결 |
| Controller 강제 종료·복귀 | 신호 중지, YouTube이벤트 유지 설정 구분, global 첫접근 계약 |
| 전체 종료·취소·저장 실패 | 정확한 확인 문구, 취소시 유지, 저장 완료 또는 실패 표시 |
| Export·로그·DB | 평문 비밀 없음, 미커밋 쓰기 성공표시 없음 |

game/render/encode 시간, FPS, 드롭, 큐, PTS gap, A/V 차이, 재접속, CPU/RAM/GPU, preview present를 계측한다. 최소화 시 회수 대상은 미리보기 전용 자원이며 송출 전체 메모리를0으로 만드는 요구가 아니다.
