# 방송 사이클과 YouTube 명령 연결 명세서

작성일: 2026-11-16  
상태: 표준 C++ 런타임 핵심·native 함수 등록·Listener 구현 및 독립 단위 검증 완료. 편집기·Controller 통합 범위는 아래 구현 상태를 구분하여 확인한다.  
대상: Rice Editor, Rice Player, Rice Controller, C++20 스크립트

방송 슬롯·송출·자격 증명은 [YouTube 연동 및 스트리밍 명세](YouTubeStreaming.md), 데이터 범위와 저장은 [Data Routing 명세](../../Engine/Data/DataRouting.md)를 따른다. 이 문서는 명령 수신, 함수 연결, 사이클 실행, 타임라인 편집을 정의한다. 일반 채팅 로그 UI는 후속 범위다.

## 1. 실행 단위와 슬롯 경계

- 사이클 스케줄러는 **게임 인스턴스마다 하나**다. 같은 인스턴스가 여러 슬롯으로 송출되어도 게임과 사이클을 복제하지 않는다.
- 각 슬롯의 채팅 입력을 같은 인스턴스의 수신 컴포넌트로 합친다. `CommandContext`에는 `InstanceId`, `InstanceGeneration`, `OriginSlotId`, `MainSlotId`, `LiveChatId`, `MessageId`, 검증된 작성자 채널 ID, 게시 시각, 수신 시각을 보존한다.
- `slot` 데이터의 해석은 입력이 들어온 슬롯이 아니라 **메인 슬롯**을 기준으로 한다. 원본 슬롯 ID는 권한 확인과 진단에 사용한다.
- 한 live chat을 서로 다른 게임 인스턴스에 동시에 연결하지 않는다. 같은 인스턴스에 같은 live chat이 여러 경로로 연결되면 수집 연결을 공유하고 `(LiveChatId, MessageId)`로 중복을 제거한다. 서로 다른 live chat의 동일한 문자열은 각각의 입력이다.
- 합친 명령 배치는 수신 시퀀스 순서로 고정한다. 수신 시퀀스는 Controller의 단일 디스패치 지점에서 부여하며, 서로 다른 방송의 게시 시각만으로 순서를 추측하지 않는다.
- 한 슬롯에서 게임을 교체하면 해당 슬롯은 이전 인스턴스에서 분리된다. 이전 인스턴스가 다른 활성 슬롯을 가지고 있으면 계속 실행한다. 마지막 활성 슬롯을 잃거나 실제 게임이 종료되면 그 인스턴스의 방송 사이클과 관련 행동을 취소한다. 새 인스턴스는 준비와 채팅 연결이 끝난 뒤 자신의 첫 사이클을 시작한다. 이미 실행 중인 인스턴스에 슬롯을 연결했다면 새 입력은 다음 배치 경계부터 합치고, 현재 사이클을 처음부터 다시 시작하지 않는다.
- 출력 슬롯 하나가 종료되어도 다른 활성 출력 슬롯이 있으면 `OnStreamEnd`를 호출하지 않는다. 메인 슬롯 여부는 출력 생명주기의 기준이 아니다.

실행 상태, 취소 토큰, 현재 인덱스, 실행 중 함수 핸들은 정적 목록에 보관하지 않는다. 정적 목록은 스크립트의 정의이며, 인스턴스별 스케줄러가 별도 상태를 가진다.

## 2. 사이클의 기준과 시간 해석

사용자가 정한 순서는 **채팅 조회 및 응답 대기 → 명령 실행 → 다음 사이클까지 남은 시간 대기**다. `OnCycle`은 명령 목록이 확정된 뒤 타임라인 0초에 호출한다.

사용자가 확정한 시간 계산은 **비차단 시간 예산**이다. `cycleTime=10초`, 실행에 3초가 걸리면 `isCycleBlocked=false`에서는 남은 7초를 기다리고, `true`에서는 실행 중 예산이 멈추므로 실행 뒤 추가 10초를 기다린다.

1. 첫 준비 단계는 게임 준비와 채팅 연결이 모두 끝나면 즉시 시작한다. 최초 조회 전에 `cycleTime`만큼 기다리지 않는다.
2. `FetchAndWait`에서 입력을 수집하고, 응답·timeout·확정 실패 중 하나로 이번 배치를 닫는다. 이때의 명령 목록은 이후 변경되지 않는다.
3. 배치를 닫은 시각을 타임라인의 `cycleStart`로 정하고 `OnCycle`을 실행한다. 명령 처리와 등록된 행동을 시작한다.
4. `cycleTime`은 `cycleStart`부터 다음 조회를 시작할 때까지의 **비차단 시간 예산**이다. 명령과 행동이 실행되는 시간도 예산에 포함하되, `isCycleBlocked` 행동으로 차단된 시간은 제외한다.
5. 예산을 모두 사용하면 현재 사이클을 닫고 다음 조회를 시작한다. 조회의 응답 대기 시간은 다음 배치 확정 전의 준비 시간이므로 직전 예산에서 차감하지 않는다.

```text
게임 준비 + 채팅 연결
  → 조회 / 응답 대기
  → 배치 확정 = cycleStart → OnCycle → 명령 및 예약 행동
  → 남은 비차단 시간 대기
  → 사이클 종료 → 다음 조회 / 응답 대기 → 다음 cycleStart
```

예를 들어 `cycleTime=10초`, 차단하지 않는 명령 처리에 2초가 걸리면 약 8초를 더 기다린다. 차단 행동이 추가로 3초 실행되면 다음 조회는 약 13초 후 시작한다. 다음 조회가 1초 걸리면 다음 `cycleStart`는 직전 시작으로부터 약 14초 뒤다. 따라서 `cycleTime`은 API 응답 시간까지 포함하는 고정 wall-clock 주기가 아니다.

구현 시 단조 시계를 사용하고 게임의 `deltaTime`, 시간 배율, 시스템 시각 변경에 의존하지 않는다. 남은 시간은 `max(0, cycleTime - 비차단 경과 시간)`이다. 예산이 이미 소진되었으면 현재 동기 호출이 반환한 뒤 종료 처리한다. 실행 중인 네이티브 함수를 강제로 중단하지 않는다.

시간 예산과 차단 중 예산 정지는 사용자 확정 사항이다. API 조회 대기를 예산 밖의 준비 단계로 구분하는 점, `prev`의 완료 기준, 무응답의 대체 기준은 상세 동작을 메운 구현 기본값이다.

## 3. 설정과 목록 API

기본 `cycleTime`은 Gladius와 같은 25초, 허용 범위는 1~3,600초다. Editor의 Streaming 설정, Controller의 대상 인스턴스 설정, 스크립트에서 변경할 수 있다. 같은 인스턴스를 여러 슬롯에서 편집할 때에도 하나의 값만 존재한다. 서로 다른 슬롯 값으로 인스턴스를 번갈아 덮어쓰지 않는다.

`Cycle.cycleTime`을 포함한 런타임 변경은 revision을 발급하여 다음 사이클의 snapshot부터 적용한다. 진행 중인 타이머·인덱스·배치는 변경하지 않는다. 적용 예정 revision과 현재 적용 revision을 UI에 구분해 표시한다.

| 항목 | 자료형 / 기본값 | 의미 |
|---|---|---|
| `Cycle` | `static Rice::List<Rice::CycleAction>` | 함수 행동의 순서 있는 목록 |
| `Cycle.cycleTime` | `double`, 25초 | 다음 조회 전까지의 비차단 시간 예산 |
| `CycleAction::from` | `enum class`, `cycleStart` | `prev`, `cycleStart`, `response` |
| `action.From(fr)` | `CycleAction&`, 기본 인자 `from::cycleStart` | 행동의 기준점을 설정하고 자신을 반환 |
| `action.timeFrom` | `float`, 0초 | 기준점으로부터의 지연; 유한한 0 이상 값만 허용 |
| `action.isCycleBlocked` | `bool`, `false` | 실행 중 예산 진행과 다른 일반 행동의 시작을 차단 |
| `action.isStoppedByCycle` | `bool`, `false` | 사이클 종료 시 해당 실행 핸들에 취소 요청 |
| `action.isActionOnNoResponse` | `bool`, `true` | 이번 조회가 유효 응답 없이 끝나도 실행 |
| `action.ActionId` | 안정적인 ID | 순서 변경과 소스 수정에도 Inspector override를 연결할 식별자 |

요청문의 `CycleAciton`은 `CycleAction`의 철자 오류로 정리한다. 초기 호환 별칭 `using CycleAciton = CycleAction;`을 제공하되 문서·UI·새 코드 생성은 `CycleAction`을 사용한다.

일반 `std::list`에는 `cycleTime`이 없으므로 엔진의 `Rice::List<CycleAction>` 특수화가 위 속성을 제공한다. `CycleTimeline`은 이 특수화의 별칭으로 사용할 수 있다. 다른 자료형의 목록에 방송 설정을 추가하지 않는다.

목록은 `Count`, `Add`, `Insert`, `Set`, `Remove`, `RemoveAt`, `Clear`, `Contains`, `IndexOf`, `Move`, `operator[]`, 순회 등 기본 기능을 지원한다. `Cycle.IndexOf(action)`이 정식 인덱스 조회 API이며 요청문의 대소문자를 위해 `Indexof` 별칭도 제공한다. 인덱스는 행동 자체가 아닌 소유 목록의 속성이므로 원문의 `CycleAction.Indexof(...)`는 이 목록 API로 정리한다. 대상이 없으면 `-1`이다. 인덱스 변경은 `Move`로 하며 조회 함수에 부수 효과를 넣지 않는다. 예약 행이 우회 수정되지 않도록 인덱스 조회는 const이고 항목 교체는 `Set`으로 한다.

`OnCycle`은 실제 타임라인의 **예약된 0번 항목**이다. 사용자가 추가한 행동은 1번부터 시작한다. 일반 항목은 목록 함수로 편집할 수 있지만 예약 항목의 삭제·이동·필드 변경은 거부한다. `Clear()`는 사용자 항목만 제거한다. 이는 이 특수화의 명시적 예외이며 UI에서도 0번 행을 잠금 표시한다.

다음은 구현된 엔진 API를 사용하는 표준 C++20 예다. 행동 함수는 즉시 완료하거나 협력적으로 poll/cancel 가능한 `ActionTask`를 반환한다.

```cpp
using Rice::CycleAction;
using Rice::Streaming::ActionTask;
using Rice::Streaming::CycleContext;

struct AudienceCycle {
    inline static Rice::List<CycleAction> Cycle{};

    static ActionTask ShowChoices(const CycleContext& execution);
    static ActionTask ApplyVotes(const CycleContext& execution);

    static void Configure() {
        Cycle.Clear(); // 예약된 OnCycle(0)은 유지한다.
        Cycle.cycleTime = 25.0;

        CycleAction show{.ActionId = "choices", .invoke = &ShowChoices};
        show.From(); // cycleStart
        show.timeFrom = 0.0f;
        show.isActionOnNoResponse = true;
        Cycle.Add(show);

        CycleAction apply{.ActionId = "apply-votes", .invoke = &ApplyVotes};
        apply.From(CycleAction::from::prev);
        apply.timeFrom = 2.0f;
        apply.isCycleBlocked = true;
        apply.isStoppedByCycle = true;
        Cycle.Add(apply);
    }
};
```

정적 `Configure`는 타입 정의를 만든다. 실행 중 스크립트에서는 해당 인스턴스의 목록 복사본을 수정한 뒤 `SetDefinition`으로 새 revision을 제출한다. 목록 복사본도 `Cycle.cycleTime` 및 같은 목록 API를 지원한다. 프로세스 전역 정적 데이터를 직접 바꾸어 다른 인스턴스의 타이머까지 변경하는 동작은 허용하지 않는다.

```cpp
auto Cycle = AudienceCycle::Cycle;
Cycle.cycleTime = 10.0;
scheduler.SetDefinition(Cycle, ++revision); // 이 인스턴스의 다음 사이클부터 적용
```

## 4. 행동 예약·완료·취소

| 기준 | 기준 시점 | 보충 규칙 |
|---|---|---|
| `cycleStart` | 이번 명령 배치 확정 시각 | 0초 행동도 `OnCycle` 이후 시작 |
| `prev` | 목록상 바로 이전 행동의 **완료** 시각 | 시작 시각 기준이 아니다. 이전 행동이 skip되면 skip 확정 시각을 완료 시각으로 사용 |
| `response` | 이번 배치에 포함된 마지막 유효 API 응답 수신 시각 | 여러 슬롯 응답은 배치에 슬롯별로 보존하고, 이 타임라인 기준은 하나로 확정 |

배치 확정 전에 이미 `response + timeFrom`이 지났으면 일반 행동은 배치 확정 직후 실행 가능 상태가 된다. 명령 목록 확정 전에 행동을 먼저 실행하지 않는다. 한 시점에 실행 가능한 항목이 여럿이면 목록 인덱스 순으로 시작한다. 이전 인덱스가 아직 미래로 예약되어 있다는 이유만으로 `cycleStart`/`response` 기준의 다른 항목을 막지 않는다. `prev`만 이전 항목 완료 의존성을 가진다.

조회가 실패하거나 timeout되어 유효 API 응답이 하나도 없으면 `hasResponse=false`다. `isActionOnNoResponse=false`인 일반 항목은 skip한다. `true`이면 실행하며, `response` 기준에는 **이번 배치의 실패/timeout 확정 시각**을 대신 사용한다. 유효 응답에 채팅이나 명령이 0개인 경우는 `hasResponse=true`다. 여러 슬롯 중 일부만 실패했다면 성공한 응답을 처리하고 실패 슬롯 상태를 컨텍스트에 별도로 담는다.

`isCycleBlocked=true` 행동이 시작되면 다음 일반 행동의 시작과 사이클 예산 시계를 멈춘다. 이미 시작된 비동기 행동, 네트워크 수신, `OnResponse`, 송출은 유지한다. 해당 행동의 완료·실패·취소 완료 시 차단을 해제한다. 한 번의 긴 동기 C++ 호출은 메인 스레드를 점유하므로 별도 호출을 즉시 끼워 넣을 수 없다. 긴 행동은 `ActionTask`와 취소 토큰을 사용하는 협력적 비동기 작업으로 작성해야 한다.

타임라인의 지연 시간도 동일한 비차단 논리 시계로 계산한다. 따라서 차단 중에는 `timeFrom` 대기 예산도 정지한다. API 응답의 실제 수신 시각은 별도 메타데이터이며, 행동 예약에는 그 응답이 수신된 논리 시각을 사용한다.

사이클 종료 시 아직 시작하지 않은 항목은 해당 사이클에서 폐기한다. 시작한 행동은 `isStoppedByCycle=true`일 때 취소 요청을 받고, `false`일 때 완료할 때까지 계속할 수 있다. 이 플래그는 임의 스레드 강제 종료를 의미하지 않는다. 종료를 무시하는 동기 함수의 결과를 되돌릴 수 있다고 보장하지 않는다.

기본적으로 같은 `ActionId`의 이전 실행이 아직 살아 있으면 다음 사이클의 중복 실행은 `Busy`로 skip하고 진단에 남긴다. 서로 다른 행동은 동시에 진행할 수 있다. 인스턴스 종료·교체·객체 파괴 시에는 `isStoppedByCycle` 값과 관계없이 해당 생명주기의 모든 행동에 취소 요청을 보내고, 이후 도착한 결과가 파괴된 게임 상태를 변경하지 못하도록 generation을 검사한다.

작업 실패는 행동 실패로 기록하고 후속 항목이 영원히 대기하지 않도록 완료 상태로 전이한다. 무한 작업으로 차단이 유지되면 Inspector/Controller에 행동 이름과 경과 시간을 표시하고 운영자의 행동 취소를 제공한다. 자동으로 일정 시간 뒤 정상 완료한 것으로 취급하지 않는다.

## 5. 이벤트 호출 계약

| 이벤트 | 실행 시점 | 보장 |
|---|---|---|
| `OnStreamStart` | 인스턴스의 활성 송출 슬롯 수가 0에서 1로 전이할 때 | 같은 인스턴스의 두 번째 슬롯 시작에는 재호출하지 않음 |
| `OnStreamEnd` | 인스턴스의 마지막 활성 송출 슬롯이 해제될 때 | 비메인 슬롯만 남아 있어도 호출하지 않음 |
| `OnCycle` | 명령 목록 확정 직후, 예약된 0번 행동 | `cycleStart + 0`, 차단하지 않음, 무응답에도 호출, 사이클 종료 취소 대상이 아님 |
| `OnResponse` | 검증된 API 응답이 도착했을 때, 일반 목록 행동보다 먼저 | 일반 행동의 차단 플래그에 의해 대기시키지 않음 |

`OnStreamStart/End`는 Rice의 출력 세션 생명주기다. 재시도 가능한 일시적 전송 오류마다 종료/시작을 반복 호출하지 않는다. 재연결 중인 슬롯은 명시적 중지 또는 terminal failure 전까지 활성 집합에 유지한다. YouTube가 신호 없이 방송 페이지를 유지하는 상태와 Rice의 활성 출력 세션을 구분한다. 영구 출력 종료 또는 명시적 해제는 해당 슬롯을 활성 집계에서 제거한다. 프로세스 강제 종료 시 마지막 콜백이 반드시 실행된다고 보장하지 않는다.

`OnCycle`에 여러 함수가 연결되면 그 함수들을 하나의 예약 행 내부에 정해진 순서로 실행한다. `OnCycle` 반환 뒤 명령 배치의 바인딩 함수를 순서대로 시작하고, 이어 일반 타임라인 행동을 시작한다. 긴 비동기 명령의 완료를 기다려야 하는 경우 그 작업을 명시적 `CycleAction` 또는 차단 작업으로 등록해야 한다.

`OnResponse`는 응답 이벤트 디스패치 우선순위가 높다. 게임 상태 변경은 메인 스레드의 다음 안전한 실행 지점에서 하며, 이미 실행 중인 C++ 함수 중간을 강제 선점하지 않는다. 여러 응답이 오면 수신 시퀀스 순서로 실행한다. callback 안에서 등록 목록을 바꾸어도 이미 확정된 배치나 현재 실행 순서를 바꾸지 않는다.

HTTP 오류·timeout은 성공 응답 이벤트가 아니다. `OnResponse`의 성공 컨텍스트 대신 연결 상태/진단 이벤트로 전달한다. 빈 성공 응답은 `OnResponse`를 호출한다. 인증·권한·연결 서비스의 오류 처리와 키 교체는 상위 스트리밍 명세를 따른다.

## 6. Pause, Resume, 교체

- 게임 Pause 시 영상 송출과 채팅 수신은 유지한다. 사이클의 게임 명령 디스패치와 행동 시작, 예산 진행을 일시정지한다. 진행 중 행동에는 pause 토큰을 전달하며, 게임 상태 변경은 pause gate를 통과해야 한다.
- Pause 동안도 API의 페이지 토큰·중복 제거 커서는 전진한다. 받은 명령은 실행 대기열에 쌓지 않고 `IgnoredDuringPause`로 처리한다. API 수신 이벤트 자체는 유지하며, `OnResponse`의 게임 상태 변경 부분은 게임 pause 규칙을 준수한다.
- Resume 시 pause 이전의 아직 실행되지 않은 명령과 pause 중의 명령을 폐기한다. 이전 사이클을 종료하고 새 조회·배치로 시작한다. 늦게 도착한 응답도 pause 구간의 게시 시각과 수신 generation을 검사하여 재생하지 않는다.
- 영상의 pause 화면·오버레이·송출 중지 여부는 상위 스트리밍 명세의 설정을 따른다. 출력이 유지되면 Pause/Resume만으로 `OnStreamStart/End`를 발생시키지 않는다.
- 게임 시간 배율을 0.5배 또는 2배로 바꾸어도 방송 사이클 시간은 달라지지 않는다.

Pause가 사용자 명령에 의해 시작되면 해당 명령 이후의 같은 배치 항목도 실행하지 않는다. `Resume`은 로컬 UI나 별도 운영 제어 경로로 처리할 수 있으며, 무시하기로 한 게임 명령 대기열을 다시 실행해 구현하지 않는다.

## 7. 타임라인 편집과 Inspector 우선순위

상단 **창 → 설정 → 사이클**에서 별도 타임라인 창을 연다. Inspector에도 선택한 행동의 같은 속성을 표시한다. 타임라인은 PowerPoint 애니메이션처럼 왼쪽에 순서·행동 이름, 오른쪽에 시간축·시작점·지속 상태를 보여준다.

- 0번 `OnCycle`은 항상 상단에 잠긴 행으로 표시한다. 나머지 행은 추가·삭제·복제·드래그 순서 변경을 지원한다.
- `cycleStart` 기준은 공통 눈금에, `prev`는 이전 행동 완료와 연결된 선에, `response`는 응답 마커와 연결된 선에 표시한다. 실행 시간이나 응답 시각을 미리 알 수 없을 때는 고정 길이처럼 그리지 않고 동적 구간으로 표시한다.
- 지연 시간, 기준점, 세 플래그를 행 또는 Inspector에서 수정할 수 있다. 현재 실행 위치, 대기 중인 의존성, 차단 원인, skip 이유를 실행 중 표시한다.
- `cycleTime`과 API 대기 시간은 별도로 표시하여 편집한 길이가 실제 시작 간격과 항상 같다는 오해를 피한다.
- 변경 예정과 현재 실행 snapshot을 구분하고, 잘못된 값은 저장 전에 표시한다. 저장은 한 revision 단위로 원자적으로 적용한다.

초기 값은 스크립트 정의다. 스크립트 적용 뒤 Inspector에서 수정한 필드는 **Inspector override가 우선**한다. source location이나 인덱스를 식별자로 사용하지 않고 `ActionId`와 필드별 override를 저장한다.

스크립트가 다시 로드되면 override가 없는 필드만 새 기본값을 따른다. 사용자가 수정한 필드는 유지한다. 소스에서 사라진 행동은 orphan으로 표시하고 실행하지 않는다. 명시적인 “스크립트 값으로 되돌리기”로 override를 제거한다. 타입이 바뀌어 override를 적용할 수 없으면 오류를 표시하고 이전 값을 임의 변환해 실행하지 않는다.

Controller와 런타임 스크립트의 변경은 현재 인스턴스의 세션 설정이며, 편집기 자산에 자동 역저장하지 않는다. Inspector override가 있는 필드를 스크립트가 변경하면 override가 유지되었음을 진단으로 알린다. Controller의 명시적 운영 변경은 유효 설정의 세션 override로 기록한다. 동시 편집은 base revision을 검사하여 충돌을 표시한다.

## 8. 명령 수신과 함수 연결 UI

입력은 `YouTube → 슬롯 → 게임 전체 명령 수신 컴포넌트 → YouTube Listener → 함수` 순서로 전달한다. 각 게임 오브젝트에 **YouTube Listener** 컴포넌트를 추가해 함수와 명령을 연결한다. 글로벌 수신 컴포넌트가 권한 검사, 순서, 중복 제거, 실행 배치를 담당하며 Listener가 별도 API 연결을 만들지 않는다.

**설정 → Streaming → YouTube Commands** 화면은 왼쪽에 명령 패턴과 사용 여부를, 오른쪽에 대상 오브젝트·Listener·함수·매개변수·실행 권한을 표시한다. 스크립트에서 선언한 패턴도 이 목록에 표시한다. Inspector에서 패턴을 새로 등록하거나 함수에 연결할 수 있다. 입력 예시를 넣으면 매칭 결과와 변환한 인자를 미리 보여준다.

바인딩은 `CommandId`, `BindingId`, `FunctionId`, 대상 객체의 안정적인 ID, 패턴, 권한, 순서, 활성 여부를 저장한다. 함수 이름 문자열만으로 런타임 주소를 찾지 않는다. 객체가 없거나 Listener가 비활성화되었으면 skip 이유를 기록한다. 실행 후 객체가 파괴되더라도 다음 바인딩에 오래된 포인터를 전달하지 않는다.

스크립트와 Inspector에서 같은 `BindingId`를 편집한 경우 하나의 바인딩에 대한 override다. 같은 함수를 두 번 실행하려면 “별도 바인딩 추가”로 다른 ID를 만든다. 복수 Listener의 전체 적용 순서는 화면에 보이는 `bindingOrder`로 직렬화하며 메모리 주소·해시 순회 순서에 의존하지 않는다.

## 9. 표준 C++ 명령 등록

사용자는 전용 Rice 문법 생성기를 사용하지 않고 **표준 C++20과 괄호가 있는 매크로·등록 함수**를 사용하기로 결정했다. 원래 요청의 대괄호 표기는 C++ 소스 문법으로 제공하지 않는다. 명령 패턴 문자열은 유지하고, 함수 포인터의 인자 형식은 템플릿으로 추론한다.

다음은 [NativeCommand.h](../../Engine/Streaming/NativeCommand.h)에 구현된 등록 API다. 하나의 X-macro 인자 목록을 실제 C++ 함수 선언과 등록에 함께 쓰므로 매개변수 이름도 같은 원본에서 나온다.

```cpp
#include "Streaming/NativeCommand.h"
using namespace Rice::Streaming;

#define DIRECTION_ARGUMENTS(X) X(int, direction), X(int, steps)

void ChangeDirection(RICE_YOUTUBE_DECLARE_PARAMETERS(DIRECTION_ARGUMENTS));

void RegisterCommands(CommandRegistry& registry) {
    registry.Add(MakeNativeBinding<&ChangeDirection>(
        "evade", "ChangeDirection",
        "!회피 {direction : (좌, -1), (중, 0), (우, 1)} {steps}칸",
        RICE_YOUTUBE_PARAMETERS(DIRECTION_ARGUMENTS)));
    registry.Add(MakeNativeBinding<&ChangeDirection>(
        "move", "ChangeDirection", "!move {direction} {steps}",
        RICE_YOUTUBE_PARAMETERS(DIRECTION_ARGUMENTS)));
}
```

`!회피 좌 3칸`은 함수에 `direction=-1`, `steps=3`을 전달한다. 패턴 캡처는 등록된 **이름으로 연결**하고, typed 래퍼가 실제 C++ 함수의 위치별 인자로 전달한다. 같은 함수에 여러 `MakeNativeBinding` 호출을 등록하면 작성된 위에서 아래 순서로 별도 패턴이 된다.

매크로 없이 `std::tuple{Parameter<int>("direction"), Parameter<int>("steps")}`로도 등록할 수 있다. 이 경우 인자 개수·형식은 C++ 컴파일러가 검사하지만, 이름 문자열과 실제 소스의 이름이 같은지는 표준 C++20으로 자동 반영할 수 없다. 이름까지 한 곳에서 관리하려면 위 X-macro를 사용한다. 등록 함수의 기본값은 `Parameter<bool>("showEffect").Default(true)`처럼 명시한다. 함수 포인터에서는 C++ 선언의 기본 인자를 복원할 수 없다.

오브젝트 멤버 함수는 `listener.BindNative<&MyScript::ChangeDirection>(bindingId, functionId, pattern, parameters)`로 등록한다. Listener는 같은 오브젝트의 현재 살아 있는 해당 컴포넌트를 실행 직전에 찾는다. Inspector에 저장하는 값은 함수 ID·패턴·순서·활성 상태이며 함수 주소는 저장하지 않는다.

| 패턴 | 의미 |
|---|---|
| `!게임 시작` | 여러 단어의 고정 명령 |
| `{steps}` | 입력 토큰을 같은 이름의 매개변수로 변환 |
| `{int steps}` | 형식을 명시한 캡처; 함수 매개변수와 변환 가능성 검사 |
| `{steps}칸` | 접미사 `칸`을 제거한 캡처를 변환 |
| `{direction : (좌, -1), (우, 1)}` | 입력 토큰을 매핑된 값으로 변환 |
| `{int direction : (좌, -1), (중, 0), (우, 1)}` | 형식을 명시한 매핑 |
| `{direction (좌, LeftValue), (우, RightValue)}` | 콜론 없는 호환 표기; 등록된 값 심볼을 매핑 |

중괄호 내부의 정식 문법은 `[type ]parameter [ : mapping-list]`이며 여기서 대괄호는 선택적 문법을 나타낸다. 매핑이 있을 때는 콜론을 권장한다. 타입 생략 시 typed 등록 함수가 추론한 형식을 따른다. 문자열에 타입을 쓴 경우 지원하는 형식 이름인지 등록 시 검사한다. 임의 C++ 문장을 채팅에서 컴파일하지 않는다.

공백은 한 개 이상의 구분자로 처리하며, 명령 앞뒤 공백은 제거한다. 고정 문자열은 기본적으로 대소문자를 구분한다. 한 캡처는 기본 한 토큰이다. 문자열 인자의 공백은 채팅 입력에서 큰따옴표로 묶는다. 접두사·접미사에 붙은 캡처는 리터럴 경계를 정확히 비교한다. 의미가 모호한 연속 캡처는 등록 시 거부한다.

매핑 키에 해당하지 않는 입력이면 **그 패턴이 매칭되지 않은 것**으로 처리한다. 함수 인자에 0이나 빈 문자열을 임의로 넣지 않는다. 동일 매핑 안에 같은 키가 여러 번 있으면 등록 오류다. 매핑 값은 리터럴, enum 상수, 등록된 데이터 심볼을 허용한다. 데이터 심볼의 값은 해당 명령 실행의 인자 snapshot을 만들 때 읽고, 함수 실행 도중 매핑을 다시 평가하지 않는다.

`{username}` 역시 이름이 `username`인 함수 인자에 채팅 문자열을 전달하는 캡처다. 인증된 작성자 정보로 자동 승격하지 않는다. 작성자 채널 ID·역할이 필요한 함수에는 별도 `CommandContext` 주입 또는 Inspector의 명시적 컨텍스트 바인딩을 사용한다. 권한 판정은 사용자 입력 문자열이 아니라 검증된 채널 ID와 역할을 사용한다.

## 10. 형 검사와 생략된 인자

함수 인자 개수·지원 형식·등록된 C++ 형식의 묵시적 변환 가능성은 템플릿 `static_assert`로 빌드 시 검사한다. 패턴은 문자열 데이터이므로 캡처 이름·매핑·생략 인자 검사는 등록/설정 저장 시 수행한다. 문자열 오류를 모두 C++ 컴파일 에러로 만들 수 있다고 보장하지 않는다. Inspector에서 만든 바인딩도 같은 함수 descriptor와 패턴 검증 경로를 사용한다.

- 이름이 없는 함수 인자, 존재하지 않는 매개변수 캡처, 한 인자에 중복된 값 공급은 오류다.
- 모든 매개변수를 패턴에 넣을 필요는 없다. 빠진 인자는 **Inspector에 명시한 고정 인자/컨텍스트 바인딩 → 등록 시 명시한 기본 인자** 순서로 구성된 공급원을 사용한다. 어느 공급원도 없으면 등록 오류이며 0 초기화하지 않는다. C++ 함수 선언에만 있는 기본값을 자동으로 알아내지 않는다.
- 매핑의 형식과 실제 함수 인자 형식은 C++ 묵시적 변환 가능성을 검사한다. 포인터·가변 인자·수명이 불명확한 참조·임의 객체 주소의 채팅 입력 변환은 허용하지 않는다. 구조체 등은 별도 등록한 파서와 값 소유권 계약이 있어야 한다.
- 채팅 문자열을 숫자로 바꾸는 작업 자체는 C++의 묵시적 변환이 아니다. 런타임 파서가 문자열 전체 소비, 범위, 부호, 유한값을 검사하고 그 뒤 템플릿 래퍼가 형식에 맞게 전달한다.
- 컴파일러가 허용하는 숫자 축소 변환도 범위 초과·값 손실이 생기면 실행 전에 거부한다. 예를 들어 `steps=3.5`를 `int` 인자에 조용히 잘라 넣지 않는다. enum은 등록된 매핑/파서를 통해서만 변환한다.
- 매개변수 형식이 변경되어 native 등록과 호환되지 않으면 컴파일러가 등록 위치를 진단한다. 문자열 패턴의 등록 이름이 달라지면 BindingId와 잘못된 이름을 진단한다. 잘못된 바인딩은 실행하지 않으며 관련 없는 정상 바인딩은 사용할 수 있다.

기본 지원은 정수·부동소수·bool·UTF-8 문자열·등록 enum이다. Data Routing이 지원하는 자료형이 곧바로 채팅에서 파싱 가능한 자료형을 뜻하지는 않는다. 추가 파서는 명시적으로 등록한다.

## 11. 다중 매칭과 실행 권한

한 입력이 여러 유효 패턴과 일치하면 **등록 목록의 위에서 아래로 모두 적용**한다. 첫 매칭만 실행하는 규칙으로 바꾸지 않는다. 같은 함수에 여러 패턴이 등록되어 있고 둘 다 일치하면 두 바인딩 모두 실행 대상이다.

매칭 목록을 확정한 뒤 바인딩마다 권한·인자 검증·객체 생존 여부를 검사한다. 허용된 항목은 위에서 아래 순으로 호출하고, 거부된 항목은 이유를 기록한다. 한 항목의 권한 부족이나 함수 실패가 뒤의 정상 항목을 숨기지 않는다. 중복 감지는 성공 실행 수가 아니라 매칭된 바인딩 수를 기준으로 한다.

다중 매칭이면 실행 결과와 별도로 다음 **오류 수준 로그**를 남긴다. 끝의 닫는 괄호까지 사용자가 지정한 형식을 유지한다.

```text
(명령어 이름) YouTubeController : Multiple controls detected from a single command)
```

구조화된 필드에는 입력 메시지 ID, 일치한 BindingId 목록, 순서, 실행/거부 결과를 저장한다. 비밀 키나 인증 토큰은 포함하지 않는다. 편집기 미리보기도 충돌을 표시하되 사용자가 의도한 다중 바인딩 자체를 금지하지 않는다.

권한은 명령별 정책으로 구성한다. 기본 운영 정책, 작성자 역할, 로컬 허용 채널 목록을 스트리밍 명세의 권한 서비스로 평가한다. `locked/open/access(...)`의 Data Routing 직접 수정 정책과 일반 함수 실행 권한은 다른 검사다. 임의 명령 함수가 허가되었다고 모든 `locked` 변수의 스트리밍 직접 수정을 허용하지 않는다.

## 12. 수집 경계와 오류 처리

Controller의 채팅 클라이언트는 YouTube가 제공하는 polling 간격과 응답 cursor를 준수한다. 게임의 `cycleTime`이 짧아도 API의 최소 조회 간격을 무시하지 않는다. 이미 승인된 수집 결과가 있으면 사이클은 그 결과의 snapshot을 사용하며, 새 결과를 기다릴 필요가 있으면 준비 단계에 머문다. 스트리밍 수신을 지원할 때에도 게임 사이클에 전달하는 경계와 순서는 동일하다.

배치는 `CycleId`와 `InstanceGeneration`을 가진다. 사이클이 이미 닫힌 뒤 도착한 응답은 현재 연결과 generation이 유효할 때 다음 배치 후보로만 사용한다. 교체된 게임·폐기된 연결·pause 구간의 명령을 새 인스턴스에 전달하지 않는다. timeout 후 실제 응답이 늦게 도착해도 같은 메시지를 두 번 실행하지 않는다.

수신 대기열과 사이클당 실행 수에는 설정 가능한 상한을 두고 초과 항목은 명시적 진단과 함께 버린다. 다음 사이클마다 과거 명령을 무한 재생하는 방식으로 backlog를 처리하지 않는다. API 장애는 빈 배치/실패 상태와 backoff로 전달하고, 송출 스레드를 대기시키지 않는다. API 키는 상위 명세처럼 오류로 교체할 이유가 있을 때만 변경한다.

YouTube의 polling 계약과 전송 인터페이스는 공식 [liveChatMessages.list](https://developers.google.com/youtube/data/live/reference/rest/v3/liveChatMessages/list), [liveChatMessages.streamList](https://developers.google.com/youtube/data/live/reference/rest/v3/liveChatMessages/streamList)를 구현 시 확인한다. 이 절은 특정 현재 호출 비용이나 quota 수치를 고정하지 않는다.

## 13. 구현 순서와 검증 기준

1. 타입 검사와 안정적인 ID를 갖는 `CommandRegistry`, native 템플릿 래퍼, 패턴 파서를 구현한다. Inspector 등록도 같은 검증 경로를 사용한다.
2. 인스턴스별 `CycleScheduler`, 불변 batch/revision, 단조 시계, 취소·pause 토큰, `ActionTask`를 구현한다.
3. 슬롯 입력 합치기, 권한 검사, Listener 수명 검사, 이벤트 순서를 연결한다.
4. Inspector/Controller 설정 동기화와 타임라인 편집을 붙인다. 비동기 작업·응답 지연·차단 구간을 실제 상태로 표시한다.

필수 검증은 다음과 같다.

| 시나리오 | 기대 결과 |
|---|---|
| 준비 완료 직후 첫 배치 | 선행 25초 대기 없이 조회; 배치 확정 뒤 `OnCycle` 1회 |
| `cycleTime=10`, 실행 3초 | 비차단이면 남은 7초 대기; 차단이면 추가 10초 대기 |
| `cycleTime=10`, 비차단 2초, 차단 3초 | 다음 조회가 시작 기준 약 13초; 조회 대기는 별도 |
| 사이클 중 길이·목록 수정 | 현재 snapshot 불변, 다음 사이클부터 revision 적용 |
| 응답 0건 / timeout | 전자는 정상 응답; 후자만 무응답 플래그에 따라 skip |
| `prev` 앞 행동이 비동기 / skip | 각각 완료 시점 / skip 확정 시점부터 지연 계산 |
| 차단 행동 중 응답 도착 | 다음 안전 지점에서 `OnResponse` 우선 디스패치 |
| 사이클 종료 / 인스턴스 종료 | 전자는 플래그에 따른 취소, 후자는 모든 생명주기 작업 취소 |
| Pause 중 명령 후 Resume | 송출·수신 유지, pause 명령 재생 없음 |
| 동일 인스턴스 3슬롯 / 메인 슬롯 출력 종료 | 하나의 사이클 유지, 마지막 출력 종료 전 `OnStreamEnd` 없음 |
| `!회피 좌 3칸` | 이름으로 `direction=-1`, `steps=3`, 생략 인자는 기본값 |
| 두 패턴에 일치 | 두 바인딩을 위→아래 적용하고 지정한 오류 로그 |
| 정수 overflow / 없는 매개변수 | 각각 런타임 변환 거부 / 빌드·등록 오류 |
| 스크립트 수정 후 Inspector 재적용 | 안정적인 ID와 필드별 override 유지 |

시간 예산과 차단 동작은 사용자 답변으로 확정되었다. 나머지 기본 해석과 제한은 이 문서에 명시한 계약으로 구현하며, 실제 네이티브 C++의 강제 선점이나 취소를 보장하는 API로 포장하지 않는다.

## 14. 현재 구현 상태와 통합 경계

현재 [CycleScheduler.h](../../Engine/Streaming/CycleScheduler.h) / [구현](../../Engine/Streaming/CycleScheduler.cpp)에 다음을 구현했다.

- `Rice::Streaming::CycleScheduler`, 인스턴스별 `CycleTicket`, 주입 가능한 단조 시계, 준비 gate, 배치 제출, 다음 사이클 revision 적용.
- `CycleActionList`와 `Rice::List<Rice::CycleAction>` 특수화, 고정된 OnCycle 0번 항목, 목록 기본 연산과 유효성 검사.
- 차단/비차단 예산, 세 기준점, 무응답 처리, 응답 우선 디스패치, 비동기 `ActionTask`, 협력적 취소·pause 토큰, 동일 행동 중복 실행 방지.
- Pause 중 배치 거부, Resume 후 이전 ticket 폐기, 재시작 시 generation 증가 검증. 응답이 도착한 시각이나 실제 메시지 게시 시각을 판정하는 수집기는 별도 통합해야 한다.

현재 [CommandPattern.h](../../Engine/Streaming/CommandPattern.h) / [구현](../../Engine/Streaming/CommandPattern.cpp)에 패턴 문자열의 런타임 메타데이터 파서를 구현했다. 여러 단어, 접두/접미 캡처, 숫자/문자열/매핑/등록 심볼, 이름 기반 인자 연결, 기본값·고정값·명시적 컨텍스트 인자, 순서 있는 다중 매칭, 권한·대상 생존 callback, 값 손실/범위 검사와 지정 오류 로그를 지원한다. 권한이 없는 항목은 데이터 심볼이나 컨텍스트 공급원을 호출하지 않는다. 이전 대괄호 형식도 문자열로 주어진 경우 메타데이터 호환 입력으로 읽지만, C++ 소스 annotation을 처리하는 기능은 아니다.

현재 지원하는 인자 형식은 int32/int64/uint32/uint64/float/double/bool/UTF-8 문자열이다. 숫자와 bool 간의 암묵적 truthiness 변환은 하지 않는다. [NativeCommand.h](../../Engine/Streaming/NativeCommand.h)의 free/member 함수 포인터 등록, 실제 인자형 추론, 이름을 공유하는 선언 매크로, 명시적 등록 defaults를 구현했다. 전용 소스 생성기는 구현 대상에서 제외한다. enum·사용자 자료형 파서 등록은 별도 확장 범위다. 현재 사이클 직접 호출 경계는 `SetDefinition`, `SubmitBatch`, `NotifyResponse`, `ActionTask`다.

[YouTubeListener](../../Engine/Streaming/YouTubeListener.h)는 Scene 컴포넌트이며 SceneCodec 저장·복원과 Inspector 컴포넌트 목록에 등록했다. 직렬화하는 override에는 주소가 없고, 실행 시 Scene instance ID·객체 ID·weak lifetime·pool generation을 검사한다. `GameCommandReceiver`는 슬롯 입력 큐, bounded message dedup, 입력 epoch, Resume 게시시각 cutoff, native Listener 디스패치를 제공한다. prefab pool에서 다시 꺼낸 객체는 현재 generation으로 `Attach`해야 한다. 같은 message ID의 배치를 재전달해도 최근 dedup 범위 안에서는 두 번 실행하지 않는다.

실제 receiver의 기본 권한은 `LocalAdmin`이며 권한 평가 callback을 연결하지 않으면 원격 명령을 거부한다. `Everyone`은 명시적으로 선택한 정책이다. 설정에는 `LocalAdmin`, `Everyone`, `Owner`, `OwnerOrModerator`, `ChannelAllowlist`와 채널 ID 목록을 보존하며, API가 검증한 작성자 정보와 Controller 권한 서비스를 연결해 평가한다.

[CycleDefinition](../../Engine/Streaming/CycleDefinition.h)은 스크립트 baseline과 필드별 Inspector override, 추가·삭제·순서, orphan 상태를 관리한다. 프로젝트의 `Streaming/cycle.json`에는 초 값을 정수 마이크로초로 저장한다. 동작 주소는 저장하지 않고 native 함수 ID를 현재 실행의 callback catalog에 연결한다. 스크립트가 로드된 뒤 `SetScriptBaseline`을 다시 호출하면 미수정 필드만 새 스크립트 값을 따른다.

[Editor StreamingPanel](../../Engine/Editor/StreamingPanel.h)을 SceneInspector에 연결했다. **Settings → Streaming → YouTube Commands**에서 좌측 명령·우측 native 함수/매개변수/권한을 편집하고, **Settings → Streaming → Cycle**에서 시간축 drag·순서·기준점·시간·세 플래그와 사이클 길이를 편집한다. 명령 설정은 scene 컴포넌트와 프로젝트의 `Streaming/commands.json`에 저장한다. 프로젝트 재진입 시 Streaming 설정 파일이 해당 Scene의 저장된 명령 override를 복원한다. 임의 C++ 소스 문자열을 실행하지 않고 등록된 함수만 선택할 수 있다.

패널의 Local test는 실제 `GameCommandReceiver`와 `CycleScheduler`를 fake clock·모의 API 응답으로 실행한다. **현재 Editor scene의 native 함수와 행동을 실제로 호출하므로 게임 데이터가 바뀔 수 있다.** 별도 테스트 scene/DB 격리는 구현하지 않았다. 상태 변경 없이 먼저 확인하는 `Match only`는 문자열 패턴 매칭만 검사하며 함수·동적 값 공급 callback을 호출하지 않는다. 숫자 변환과 실행 권한의 최종 검사는 실행 경로에서 한다. 이 경로는 신뢰한 로컬 운영자의 테스트이며 YouTube API를 호출하지 않는다. **실제 Player↔Controller IPC로 채팅을 받아 게임 함수에 전달하는 전체 실행 경로는 이 패널의 로컬 테스트와 별도 통합 항목이다.**

네트워크 작업자는 이 객체를 동시에 호출하지 않고 소유 스레드로 결과를 전달해야 한다. 취소 상태의 플래그는 작업자가 읽을 수 있는 atomic 값이지만, 스케줄러와 목록·receiver 자체는 단일 소유 스레드 API다. verified channel·message 게시시각의 실제 API 추출, receiver 입력 epoch 연결, 슬롯 연결 수명, backoff는 Controller/수집기 통합의 책임이다.

[StreamingCycleTests.cpp](../../Tests/StreamingCycleTests.cpp)와 [CommandPatternTests.cpp](../../Tests/CommandPatternTests.cpp)는 실제 C++20 코드를 독립 실행 파일로 컴파일해 검증한다. 테스트는 fake clock을 사용하여 10초/3초 차단 규칙, 조회 대기 분리, 응답 우선순위, `prev` 완료 기준, revision, 무응답, 취소, Pause, stale generation, 등록 목록 변경 중 디스패치, 사용자 한국어 명령 예제, 형식 오류, 다중 매칭과 권한을 검증한다.

[NativeCommandTests.cpp](../../Tests/NativeCommandTests.cpp)와 [YouTubeListenerTests.cpp](../../Tests/YouTubeListenerTests.cpp)는 실제 free/member C++ 함수 호출, X-macro 이름 공유, 기본 인자 명시, 파괴·제거·pool 재사용, Inspector override 직렬화, 슬롯 경계·Pause·배치 재생 차단을 검증한다.

[CycleDefinitionTests.cpp](../../Tests/CycleDefinitionTests.cpp)는 필드별 우선순위·스크립트 재로드·orphan·JSON 왕복·파일 교체·파일 크기 제한을 검증한다. [StreamingPanelTests.cpp](../../Tests/StreamingPanelTests.cpp)는 실제 ImGui 컨텍스트에서 두 창을 그리며 local native 명령/사이클 실행과 프로젝트 설정 복원을 검증한다.
