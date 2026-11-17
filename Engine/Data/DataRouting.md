# Data Routing 명세서

작성일: 2026-11-16  
상태: 최신 사용자 결정 반영 / native C++ 저장 기반 구현 / Player 및 송출 통합 진행 중  
대상: Rice Engine 데이터 전용 C++ 스크립트 클래스, Rice Player, Rice Controller

## 1. 목적과 확정 범위

데이터 선언의 세 분류로 **공유 범위**, **수명**, **외부 수정 권한**을 지정한다. 최신 결정에 따라 표준 C++ 매크로/등록 함수와 native proxy를 사용한다. 게임 임시값은 메모리, 슬롯값과 영구값은 **내장 SQLite**다. Redis/PostgreSQL 서버 설치는 필요하지 않다. 추후 서버형 유료 API는 별도 저장 서비스 어댑터로 확장하며 현재 범위에는 포함하지 않는다.

슬롯은 **Controller의 방송 항목**이다. 기존 수명 키워드 `stream`은 **`slot`으로 변경**한다. 슬롯값은 방송 종료 때 삭제하는 임시값이 아니라 슬롯과 게임 종류의 조합에 남는 영속 데이터다. 슬롯·송출 수명은 [YouTube 연동 및 스트리밍 명세](../../docs/requirements/YouTubeStreaming.md), 사이클·명령은 [사이클 및 명령 명세](../../docs/requirements/CycleAndCommands.md)를 함께 따른다.

구현 대상은 native 선언/타입 검사·게임 연결·슬롯 저장/관리·영구 저장·global의 매 접근 SQL·Controller 단절 시 로컬 저장과 재접속 규칙이다. **변수 직접 수정용 원격 ACL만 더미**로 남긴다. YouTube 명령 실행 권한과 Controller 운영자의 데이터 편집 권한은 별도로 구현한다.

라우팅은 표시된 **데이터 전용 클래스**에만 적용한다. 내부 상태·객체 포인터·렌더링 캐시·일반 스크립트 실행 상태는 별도 클래스로 분리한다. `locked`도 게임 코드와 Controller의 명시적 운영자 편집을 금지하지 않는다.

## 2. 수식어와 기본값

| 분류 | 키워드 | 의미 |
|---|---|---|
| 공유 범위 | `build` 또는 생략 | 안정적인 논리 BuildId에 속한 값 |
| 공유 범위 | `global` | 같은 StorageDomainId의 모든 빌드가 동일 DataId/ObjectKey로 공유하는 영구값 |
| 수명 | `game` 또는 일반 선언에서 생략 | 현재 GameSessionId의 메모리값. 게임 종료 때 해제 |
| 수명 | `slot` | 주 슬롯과 게임 종류에 연결되는 SQLite 영속값 |
| 수명 | `stored` | 슬롯에 종속되지 않는 SQLite 영구값 |
| 외부 수정 권한 | `locked` 또는 생략 | 향후 스트리밍 직접 변수 수정 금지 |
| 외부 수정 권한 | `access(policy)` | 향후 인증된 YouTube 계정과 정책을 비교하여 직접 수정 허용 |
| 외부 수정 권한 | `open` | 향후 스트리밍 직접 변수 수정 허용 |

일반 생략값은 **`build game locked`**다. 단, **`global`의 생략 수명은 `stored`**다. `global int count = 0;`은 유효하며 `global stored locked`와 같다.

유효 조합은 `build game`, `build slot`, `build stored`, `global stored` 네 가지다. 각 조합에 세 권한을 붙인다. `global game`, `global slot`, 과거 표기 `global stream`은 모두 빌드 오류다. 잘못된 수식어가 global의 영구 라우팅을 바꾸지 못하게 하며, 오류를 출력하고 빌드를 중단한다. 오류를 무시한 성공 실행 파일을 만들지 않는다.

요청한 진단 문자열은 다음과 같다. 현재 명칭인 slot은 보조 설명에 추가하되 이 문자열은 유지한다.

```text
(변수명과 위치) : global variables can't be game(stream) temporary.
```

`global`은 C++ 전역 변수나 `static`을, `stored`는 상수를 뜻하지 않는다. 권한은 저장 위치·수명을 바꾸지 않는다.

## 3. 표준 C++ 선언과 설명

### 3.1 등록 방식

최신 결정에 따라 **전용 Rice 문법·소스 변환기·Clang 생성기 없이 표준 C++20만 사용**한다. 괄호 있는 선언 매크로 또는 등록 함수를 허용한다. 이전의 `slot int value` 원본 변환 계획은 폐기한다.

`RICE_GAME_FIELD`, `RICE_SLOT_FIELD`, `RICE_STORED_FIELD`, `RICE_GLOBAL_FIELD`가 `Field<T>`를 선언하고 DataContext에 등록한다. 영속 필드는 이름 변경 후에도 키가 유지되는 `*_FIELD_ID` 형태를 권장한다. 축을 생략하는 `context.Bind<T>()`의 기본은 build/game/locked, Scope::Global을 지정하면 기본 수명은 stored다. **등록하지 않은 평범한 int는 자동 라우팅하지 않는다.** 일반 C++ 변수의 모든 읽기를 매크로가 가로챌 수는 없다.

```cpp
#include "Engine/Data/DataRoutingMacros.h"
using IntList = std::vector<int>; // 콤마가 있는 템플릿 타입도 alias로 매크로에 전달

struct GameData {
    Rice::Data::DataContext& context; // 필드보다 먼저 초기화
    explicit GameData(Rice::Data::DataContext& c) : context(c) {
        Data.Description("이 슬롯에서 유지하는 게임 점수");
    }

    RICE_GAME_FIELD_ID(context, int, score, "game/score", 0);
    RICE_SLOT_FIELD_ID(context, int, Data, "game/slot-score", 100);
    RICE_SLOT_FIELD_ID(context, IntList, items, "game/items", IntList{});
    RICE_SLOT_FIELD_ID(context, glm::vec3, position, "game/position", glm::vec3(0));
    RICE_GLOBAL_FIELD_ID(context, std::string, announcement, "shared/announcement", "");
};

void Example(GameData& Data) {
    Data.score += 1;
    Data.items.push_back(10);
    Data.items[0] = 20;
    Data.position.x = 1.5f;
}
```

데이터 전용 객체마다 DataContext를 사용한다. 내부 상태·렌더 캐시·일반 객체 포인터는 별도 상태 클래스에 둔다. BuildId/DataId/ObjectKey와 game session, 주 슬롯 문맥, 저장소를 Context가 제공한다. 메모리/SQLite/global 저장소는 Context와 Field보다 오래 살아 있어야 한다.

일반 선언 매크로 `RICE_DATA_FIELD_ID(ctx, Type, Name, StableId, Default, Scope, Lifetime, Permission)`가 세 분류를 명시한다. 권한은 AccessPolicy 메타데이터이며 직접 원격 쓰기 ACL은 아직 더미다. private 멤버 여부와 원격 권한은 별개다.

### 3.2 설명

원래 요청한 `slot Data.Description("설명");`의 의도를 표준 C++의 **`Data.Description("설명");`**으로 구현한다. Data는 이미 slot으로 등록한 변수명이다. 데이터 객체 생성자 등 정상 C++ 문장 위치에서 호출한다. 전역 Data 서비스나 타입명이 아니다.

설명은 기본 메타데이터다. 저장값 생성 전에 호출하면 기본 설명에 반영된다. 이미 Controller가 저장한 사용자 설명을 재빌드나 일반 게임 대입으로 덮어쓰지 않는다. “모두 초기화”는 선언 기본값/이름/설명으로 복원하고 스키마 없는 수동 행은 삭제하는 상위 관리 계약이다.

### 3.3 사용자 구조체 등록

구조체의 **필드 목록을 명시적으로 등록**하여 네이티브 값 코덱과 이름 있는 proxy 멤버를 함께 만든다. 다음 선언도 표준 C++ 매크로 확장뿐이며 별도 컴파일러가 필요 없다.

```cpp
using Rewards = std::vector<int>;
struct PlayerData {
    int score = 0;
    glm::vec3 position{0};
    Rewards rewards;
};

#define PLAYER_DATA_FIELDS(X) \
    X(int, score) \
    X(glm::vec3, position) \
    X(Rewards, rewards)

RICE_DATA_STRUCT(PlayerData, "my-game/player-data", 1, PLAYER_DATA_FIELDS)
```

이후 `Field<PlayerData> Data`는 `Data.score++`, `Data.position.x = 2`, `Data.rewards.push_back(3)`를 지원한다. 등록 필드 타입과 실제 멤버 타입이 다르면 static_assert가 실패한다. 비공개 멤버는 표준 C++ 접근 규칙을 따르므로 초기 등록 예제는 공개 데이터 구조체다. 스키마 필드 변경은 버전 변경 및 migration이 필요하다.

### 3.4 타입과 코덱

저장 조건은 **DataCodec<T>의 타입 태그·버전·검증·encode/decode 계약**이다. 코덱이 있는 구체화 템플릿도 허용한다.

| 타입 | 지원 |
|---|---|
| bool, 고정 폭 정수, int, float/double, UTF-8 string | 기본 코덱, 유한 실수/문자열 검증 |
| Vector2/3 = glm::vec2/3 | 성분 코덱과 x/y/z proxy |
| std::array<T,N>, std::vector<T>, std::list<T> | 원소 코덱과 길이 한도, 인덱스 proxy 및 가능한 목록 변경 함수 |
| 사용자 구조체 | RICE_DATA_STRUCT로 코덱+멤버 proxy 등록 |
| 다른 구체화 템플릿 | 명시적 DataCodec<T>로 저장 가능. 직접 멤버/메서드 표기는 별도 facade 등록 필요 |
| 포인터/참조/소켓/파일/엔진 핸들/mutex | 직접 저장 금지. 안정적인 ID 등의 저장 표현으로 변환 |

고정 배열은 반환·복사가 가능한 std::array를 사용한다. C 스타일 T[N]은 직접 Field의 값 타입으로 사용하지 않는다. 원시 메모리·padding·주소를 저장하거나 포인터를 따라 순환 그래프를 자동 저장하지 않는다.

저장값이 기본값보다 우선한다. 없는 경우에만 등록 초기값을 사용한다. 읽기/쓰기에서 코덱이 일치하지 않으면 명시적인 오류다. 정수 overflow·비유한 실수·잘못된 UTF-8·과도한 길이를 거부한다.

## 4. native proxy의 동작과 한계

### 4.1 직접 표기를 관찰하는 방법

`Field<T>`와 `ValueProxy<T>`가 표준 C++ 연산자를 통해 저장소를 호출한다. 숫자의 읽기·대입·산술 대입·증감, 목록의 push_back/emplace_back/pop_back/clear/resize, 인덱스 대입, 등록 구조체/벡터의 멤버 변경을 관찰한다. 별도 Get/Edit 호출이 일상적인 직접 변경의 필수 조건은 아니다.

`Data.items.push_back(x)`는 현재 저장값을 읽어 변경하고 저장한다. `Data.position.x = x`는 등록된 x proxy가 루트 구조체/벡터의 변경을 전달한다. global의 변경은 저장 서비스의 Mutate를 사용하며 해당 트랜잭션 안에서 읽기·변경·코덱 검증·커밋한다. 메모리/일반 영속 proxy는 버전 검사로 동시 변경 충돌을 알린다. 모든 DB 호출은 저장/스크립트 작업 문맥에서 수행한다.

mutable 원시 포인터·참조·iterator를 외부에 주지 않는다. `auto item = Data.items[0]`은 현재 라우팅 값을 가리키는 안전한 proxy handle이며 `int&`이 아니다. Field/proxy 복사 생성은 같은 라우트의 핸들을 복사하고, 할당은 값을 읽어 대상에 저장한다. 다른 게임 객체를 만들 때는 새 Context/ObjectKey로 생성한다.

### 4.2 표준 C++ 한계

C++에서는 일반 operator.를 오버로드할 수 없다. 따라서 **등록한 멤버와 구현한 컨테이너 연산만 직접 표기**를 지원한다. 사용자 구조체의 임의 메서드나 모든 표준 라이브러리 함수를 자동 가로챈다고 약속하지 않는다. 코덱이 있다는 사실만으로 해당 타입의 모든 메서드 facade가 생기지는 않는다.

템플릿 인자 추론을 수행하는 외부 함수(예: 일부 glm 함수)에는 `Data.position.Read()`로 값 스냅샷을 전달할 수 있다. 복잡한 지원 밖 변경에는 명시적인 Edit/Mutate 확장 API를 사용할 수 있으나 일반 목록/등록 멤버의 직접 변경은 그대로 제공한다. iterator 기반 erase/insert 대신 인덱스 기반 erase(index)/insert(index,value)를 제공한다. sizeof/주소/int& 등 네이티브 변수 ABI와 동일하다고 보장하지 않는다.

### 4.3 컴파일과 진단

일반 C++20 컴파일러가 매크로 인자 수·타입·템플릿 제약·접근 지정자를 검사한다. 별도 소스 생성기나 전처리 검사를 필수화하지 않는다.

| 조건 | 검사 |
|---|---|
| global + game/slot | 선언 매크로 및 Context Bind의 static_assert, 요청한 오류 문자열 |
| 코덱 없는 타입 또는 등록 멤버 타입 불일치 | 템플릿/개념/static_assert 컴파일 오류 |
| 지원하지 않는 메서드·원시 참조 변환 | 일반 C++ 멤버 조회/타입 오류 |
| 잘못된 매크로 인자·순서 | 일반 C++ 전처리/컴파일 오류 |
| 중복 DataId, 슬롯/게임 문맥 없음 | 등록 시 오류 |
| 다른 배포 빌드의 스키마 충돌 | DB 연결/최초 접근 시 오류 |

과거 stream 수명은 공개 enum/선언 매크로로 제공하지 않는다. 이전 문법의 정확한 원본 위치 진단용 RiceDataGen 계획은 폐기한다.

## 5. 식별자와 공유 경계

| 식별자 | 역할 |
|---|---|
| StorageDomainId | 운영 저장소 공유 경계. 에디터 테스트와 운영은 별도 |
| BuildId / InstanceTypeId | 게임 종류의 안정적인 논리 ID. 초기 InstanceTypeId는 BuildId 참조 |
| BuildRevision | 실제 빌드 버전. 기본 저장 키에는 포함하지 않음 |
| SlotId | Controller 방송 항목의 영속 UUID. 이름·순서·주소·키와 무관 |
| GameSessionId / InstanceRunId | 이번 실행의 ID. 영속 데이터 키에 넣지 않음 |
| DataId | 필드의 안정적인 논리 ID. 이름 변경·파일 이동 후 유지 |
| ObjectKey | singleton, 저장 객체 GUID 등 값의 소유 객체 구분 |
| BindingGeneration | 현재 객체/주 슬롯 바인딩 세대. 이전 응답 거부 |

```text
build game   : Domain / BuildId / GameSessionId / DataId / ObjectKey
build slot   : Domain / SlotId / InstanceTypeId(BuildId) / DataId / ObjectKey
build stored : Domain / BuildId / DataId / ObjectKey
global stored: Domain / DataId / ObjectKey
```

실제 키는 길이 정보를 포함해 구분자 충돌을 막는다. BuildId와 DataId는 소스 관리되는 manifest에 고정하여 Export한다. 재빌드나 이름 변경만으로 새 ID를 만들지 않는다. 빌드들이 global을 공유하려면 같은 DataId와 호환 스키마를 명시적으로 사용한다. 이름이 같다고 자동 공유하지 않는다.

영속 키에 프로세스 ID·주소·Controller runId·현재 런타임 EntityID를 사용하지 않는다. 여러 객체의 영속 필드에는 재실행 후 재구성할 안정적인 ObjectKey가 필수다. 동일 공유 키는 쓰기 주체 하나를 기본으로 하고, 동시 누적은 저장 서비스의 원자적 Increment/트랜잭션을 사용한다.

## 6. 슬롯 라우팅과 데이터 관리

### 6.1 주 슬롯과 여러 출력

한 슬롯은 동시에 한 게임 인스턴스만 연결한다. 동일 인스턴스는 여러 슬롯으로 송출할 수 있다. 처음 인스턴스를 지정한 슬롯을 주 슬롯으로 자동 선택하며 운영자가 바꿀 수 있다. 방송 시작 전에 주 슬롯이 확정되어야 한다.

인스턴스의 **모든 slot 변수는 주 슬롯의 SlotId만 사용**한다. 보조 슬롯별로 복제하거나 여러 슬롯 값을 병합하지 않는다. 보조 슬롯 채팅으로 실행한 함수도 현재 주 슬롯 값을 읽고 쓴다. 모든 슬롯의 입출력은 동시에 유지하지만 같은 인스턴스의 slot 저장 문맥은 하나다.

주 슬롯 출력이 중단돼도 다른 활성 슬롯이 있으면 인스턴스 방송은 끝나지 않는다. 주 슬롯 ID와 데이터 문맥도 자동 변경하지 않는다. 슬롯에서 게임 A를 B로 바꾸더라도 A가 다른 슬롯에서 활성 상태면 A의 사이클/바인딩은 유지한다.

주 슬롯 변경은 다음 사이클 경계 등 안전한 실행 경계에서 처리한다. 새 명령 실행을 잠시 막고 이전 값 커밋 → 새 슬롯 snapshot 및 스키마 검증 → 한번에 rebind 순서로 적용한다. 실패하면 기존 문맥을 유지한다. 진행 중인 작업은 완료 또는 취소를 확정한 뒤 전환하고 BindingGeneration을 증가시킨다. 이전 슬롯 대상 비동기 쓰기를 새 슬롯으로 잘못 전달하지 않는다.

### 6.2 교체·복원·기본값

- 슬롯 A의 게임 G를 H로 교체해도 G 행은 남는다. 나중에 G를 같은 주 슬롯 A에서 실행하면 복원한다.
- G의 주 슬롯을 B로 바꾸면 B 값을 로드한다. 없으면 G의 선언/씬 기본값을 원자적으로 생성한다. A의 현재 값을 B 기본값으로 복사하지 않는다.
- BuildRevision 변경만으로 초기화하지 않는다. 타입 변경은 명시적인 migration이다.
- 방송 종료·Pause·미리보기 숨김·Player 종료·Controller 재시작은 slot 행 삭제 사유가 아니다.
- SlotId 자체를 삭제하는 작업은 그 슬롯의 전체 데이터 삭제를 포함한다. 단순 게임 교체·송출 중지는 슬롯 삭제가 아니다. 활성 인스턴스의 주 슬롯 삭제에는 먼저 대체 슬롯 선택 또는 인스턴스 종료가 필요하다.
- 독립 Player의 slot 필드에는 실제 SlotContext가 필요하다. 없으면 연결 실패를 표시하고 임의 가짜 슬롯으로 저장하지 않는다.

### 6.3 Controller 관리 화면

슬롯의 “관리”에서 방송 설정과 “데이터 관리”를 연다. 게임 종류별로 묶고 각 항목에 **이름·설명·값**, 삭제·추가·모두 초기화 버튼을 제공한다. DataId/ObjectKey/타입/version은 상세 정보로 표시한다.

| 조작 | 계약 |
|---|---|
| 값 수정 | 타입·범위·길이 검증 후 CAS 커밋. 활성 주 슬롯은 안전 지점에 적용 |
| 이름 수정 | 표시 이름 수정. 스크립트 식별자와 안정적인 DataId는 유지 |
| 설명 수정 | 사용자 설명 저장. 소스 기본 설명보다 우선 |
| 추가 | 선택 게임의 스키마 필드 선택 또는 타입 지정 수동 행 생성. 이름·설명·값 입력 |
| 삭제 | 값/사용자 메타데이터 제거. 선언된 활성 필드는 기본값으로 재바인딩, 미사용/수동 행은 제거 |
| 모두 초기화 | 표시된 선택 게임 종류의 행을 원자적으로 초기화. 선언 필드는 기본값, 수동 행은 삭제 |
| 슬롯 전체 삭제 | 방송 항목 제거와 함께 모든 게임 종류의 데이터를 삭제 대상으로 표시 |

초기화 기본 범위는 선택 게임 종류다. 범위를 넘길 때는 “이 슬롯의 모든 게임 데이터”를 명시한다. 활성 데이터 삭제/초기화는 generation을 갱신하여 늦은 응답이 삭제 전 값을 되살리지 못하게 한다. UI는 DB 커밋과 게임 적용 상태를 구분한다.

추가 화면에 새 이름을 입력한다고 스크립트 멤버가 생기지 않는다. 수동 행에는 고유 DataId/명시적 타입을 부여하고 나중에 스키마에 연결하려면 ID 매핑을 명시한다. 이름 일치만으로 연결하지 않는다.

이는 로컬 운영자 작업이므로 **locked도 수정 가능**하다. 시청자에게 운영자 우회 권한을 부여하지 않는다. UI는 SQLite row를 직접 고치지 않고 DataService의 검증·버전·감사 경로를 사용한다.

현재 Controller 편집기는 scalar 외 Vector2/3/4와 기본 코덱으로 구성된 중첩 배열·목록을 JSON 배열로 표시/수정한다. uint64와 실수는 double로 중간 변환하지 않는 정확한 숫자 토큰을 사용한다. 고정 길이·숫자 범위·중첩 타입을 검사한다. RICE_DATA_STRUCT가 내보낸 DataTypeSchema를 전달받은 경우 구조체 JSON 객체도 편집할 수 있다. 스키마가 없는 사용자 타입은 바이트 배치를 추측하지 않고 값 읽기 전용이며 이름·설명·삭제만 허용한다. 현재 Player→Controller 스키마 전송은 별도 통합 작업이다.

“이 슬롯의 모든 게임 보기”는 명시적인 조회 범위다. “이 게임 초기화”와 “슬롯 전체 초기화”는 별도 확인창을 사용한다. 전체 조회에서 추가하는 항목에도 게임 데이터 ID를 반드시 입력한다. 전체 초기화 API는 지정한 도메인/슬롯의 slot 데이터만 삭제하며 다른 슬롯·stored·global 데이터는 포함하지 않는다.

## 7. global의 SQL 접근과 Controller 단절

### 7.1 매 읽기·쓰기의 의미

global은 **논리적 읽기마다 SELECT, 쓰기마다 SQL 검증/트랜잭션**을 실행한다. 연결 중 Controller DataService의 DB가 권위 저장소이며 단절 중에는 Player 로컬 SQLite가 같은 접근 인터페이스를 제공한다.

Read()로 받은 복사본을 여러 번 사용하는 것은 한 읽기다. 별도 접근자 호출은 각각 SQL에 도달하며 DB 트랜잭션 없이 메모리 캐시만 반환하지 않는다. 원자적 읽기-수정-쓰기는 Increment/Mutate다. 별도의 `x = x + 1`은 분산 원자 연산이 아니다.

SQL/IPC에는 지연이 있다. **송출·렌더·오디오 스레드는 저장소를 기다리지 않는다.** 비동기 접근을 기본으로 하고 스크립트 작업만 완료를 기다리거나 yield한다. 동기식 변수 사용감을 제공하는 브리지는 전용 스크립트 실행 문맥에서만 허용한다. 현재 메인 스레드 콜백에 임의 동기 읽기를 허용하면서 “항상 최신 SQL”과 “절대 대기 없음”을 동시에 보장할 수는 없다.

### 7.2 로컬 임시 보관

연결 중 확인한 global 값과 schema/version을 Player의 **fallback SQLite**에도 기록한다. Controller가 없어지면 로컬 DB에서 계속 읽고 쓴다. fallback은 임시 대체본이지만 프로세스 장애에 대비하여 디스크에 보존한다. 단절 중 다른 Player와 즉시 공유되지 않으며 상태는 LocalFallback으로 표시한다.

권위 DB와 fallback은 별도 파일이다. 같은 파일을 두 프로세스가 서로 다른 권위로 쓰지 않는다. fallback에는 값·타입·기준 version·local version·단절 세대·요청 ID를 보존한다. Controller 단절 시 Player가 생존할 수 있고, Player도 종료되는 경우 마지막 로컬 커밋은 다음 실행에서 복원한다. 송출 중단 여부는 스트리밍 명세가 결정한다.

로컬에 값을 받은 적이 없으면 등록 스키마의 기본값으로 행을 만들고 UnseededFallback을 표시한다. 이 초기화 자체는 재접속 첫 논리 접근이 아니다. 스키마가 없거나 로컬 DB가 실패하면 명시적 오류이며 메모리값을 저장 성공으로 보고하지 않는다.

### 7.3 재접속 첫 연산 규칙

판정은 **DataId/ObjectKey를 포함한 데이터 키별**이다. 재접속 후 첫 실제 스크립트/명령 접근이 읽기인지 쓰기인지 결정한다. handshake·health check·스키마 등록·자동 UI 조회는 판정을 소비하지 않는다. 운영자의 실제 편집은 쓰기다.

| 첫 연산 | 확정 동작 |
|---|---|
| 읽기 | 권위 global을 읽어 로컬을 교체하고 반환. 단절 중 dirty 값도 자동 업로드하지 않음 |
| 쓰기 | 해당 쓰기를 반영한 로컬 후보값으로 권위 값을 덮어쓰고 version 증가 |
| 원자적 Mutate/Increment | 쓰기 계열. 로컬 후보에 연산을 정확히 한 번 적용한 결과로 최초 덮어쓰기 |
| 아직 접근 없음 | 자동 조정 없이 미결 상태 유지 |

단절 전 10 → 로컬에서 20, 다른 빌드가 권위 값을 30으로 바꿨다면 첫 읽기는 **30**을 반환하며 로컬 20을 폐기한다. 첫 Write(40)은 권위 값을 **40**, 첫 Increment(1)은 로컬 20에 적용한 **21**로 만든다. 자동 시간 비교·합산·병합하지 않는다.

원자성·재시도 규칙:

1. 키별 접근을 직렬화하고 안정적인 clientId/reconnectEpoch/key/requestId를 부여한다. 같은 키의 경쟁은 서비스가 수락한 첫 연산 순서로 결정한다.
2. 서비스는 최초 연산 결정·값 조회/변경·version·응답 기록을 한 트랜잭션으로 확정한다. 읽기-first도 당시 읽은 값/version을 기록하여 응답 유실 재시도가 새로운 판정을 만들지 않게 한다.
3. 응답 유실은 같은 ID로 재시도/조회한다. 같은 ID의 다른 payload는 거부한다. 로컬에도 결과 적용과 응답 ID를 트랜잭션으로 보존한다.
4. 같은 재접속 세대의 재시도가 첫-write 권한을 반복 사용하지 않는다. 이후 쓰기는 통상 expectedVersion CAS다.
5. 여러 Player의 최초 쓰기는 권위 DB 트랜잭션 순서로 직렬화한다. 나중의 명시적 최초 덮어쓰기가 이길 수 있으며 기록을 남긴다. 키별 쓰기 소유자 하나를 권장한다.
6. IPC timeout만으로 미확인 권위 쓰기를 fallback에서 중복 실행하지 않는다. UnknownCommit을 보존하고 재접속 후 request ledger에서 결과를 확인한 뒤 새 조정을 진행한다. 프로세스 재시작도 미완료 세대를 복구한다.

최초 쓰기는 권위 version이 더 새로워도 덮어쓰는 **사용자 지정 예외**다. 일반적인 자동 충돌 병합이나 오프라인 전체 업로드를 추가하지 않는다.

## 8. SQLite와 커밋 계약

권위 DB 기본 경로는 `%LOCALAPPDATA%/Rice/Data/<StorageDomainId>/data.sqlite3`다. fallback은 `fallback/<ClientId>.sqlite3`로 분리한다. 경로에는 검증된 내부 ID만 사용한다. 프로젝트/Export/실행 파일 디렉터리에 런타임 DB를 쓰지 않는다.

연동 모드는 Controller의 전용 DataService 작업 큐가 DB를 처리한다. 독립 build stored는 내장 SQLite를 사용한다. 연결 중 같은 Player가 DB 직접 접근과 IPC를 동시에 사용하지 않는다. global은 7절 fallback 계약을 사용하며 단독 slot은 실제 SlotContext가 필요하다.

- WAL, 쓰기 연결 synchronous=FULL 및 foreign_keys=ON을 설정하고 결과를 확인한다. WAL은 로컬 디스크에서만 사용한다. writer는 하나이므로 긴 트랜잭션을 피한다. [SQLite WAL](https://www.sqlite.org/wal.html), [동기화 설정](https://www.sqlite.org/pragma.html#pragma_synchronous)
- busy_timeout/retry에는 전체 deadline을 둔다. checkpoint/backup/migration은 유지보수 작업이며 송출 스레드에서 실행하지 않는다.
- 값은 type tag/codec version을 가진 정규 이진 표현이다. uint64는 signed INTEGER로 손실 변환하지 않고 8-byte BLOB으로 보존한다. IPC에서도 64-bit 값은 타입 있는 10진 문자열 또는 바이너리다. [SQLite 저장 타입](https://www.sqlite.org/datatype3.html)

| 논리 테이블 | 필수 정보 |
|---|---|
| route_schema | DataId, scope/lifetime, 타입/코덱/스키마 버전, 기본 이름/설명/값, 정책 fingerprint |
| route_values | 완전한 route key PK, value BLOB, 타입/코덱, 양수 version, 수정 시각 |
| slot_data_metadata | SlotId/InstanceTypeId/DataId/ObjectKey, 사용자 이름/설명, 수동 행 여부 |
| route_requests | client/request 고유 키, payload hash, 응답, 적용 version, 요청 유효기간 |
| reconnect_decisions | client/epoch/key 고유 키, 최초 연산 종류/ID, 결과 version/응답 |
| fallback_state | 로컬 기준 version/local version/세대, dirty/UnknownCommit 상태 |
| schema_migrations | migration ID·적용 시각·DB 버전 |

통상 쓰기는 expectedVersion CAS다. 값·version·요청 결과를 같은 트랜잭션으로 커밋한다. 최초 생성은 충돌 확인 INSERT이며 Increment는 디코딩·범위 검사·갱신을 한 트랜잭션에서 처리한다. 7.3절의 최초 쓰기만 덮어쓰기 예외다. ledger 정리 후 오래된 요청이 다시 신규 요청으로 실행되지 않도록 유효기간을 검사한다.

성공은 SQLite COMMIT과 응답 기록이 확정된 시점이다. 큐 대기는 Pending, 실패는 Failed, fallback 커밋은 LocalCommitted로 구분한다. **중요한 YouTube 명령은 데이터 커밋 확인 후 성공 응답**한다. 게임 외부 부수효과까지 DB와 자동 분산 트랜잭션으로 묶인다고 보장하지 않으며 필요한 명령은 요청 ID와 재실행 안전성을 설계한다.

정상 Stop/전체 종료는 제한 시간 동안 flush하고 실패를 표시한다. 강제 종료의 미커밋 작업은 유실될 수 있다. busy·디스크 부족·읽기 전용·I/O·손상을 구분하며 DB 자동 삭제나 빈 DB 대체를 성공으로 표시하지 않는다.

migration은 백업 후 단일 소유권/트랜잭션으로 수행하고 미지원 최신 버전은 거부한다. WAL 사용 중 본체 파일만 복사하지 않고 [SQLite Backup API](https://www.sqlite.org/backup.html) 등 일관된 스냅샷을 사용한다. 복원은 연결을 종료한 유지보수 상태에서 검증한다.

YouTube OAuth token·Data API key·송출 key는 변수 테이블에 평문 저장하지 않고 OS 보안 저장소 참조를 저장한다.

## 9. 바인딩과 권한

Play는 실행 객체 생성 → GameSessionId/BindingGeneration → 스키마 확인 → 주 슬롯/저장소 연결 → 복원 → Ready → Start 순서다. 현재 AddComponent가 즉시 Awake를 부르는 구조에서는 **Start 전 복원**을 보장한다. 별도 초기화 순서 변경 없이 Awake에 복원값을 약속하지 않는다.

Stop은 새 명령/쓰기 중단 → 진행 작업 완료/취소 → 영속 커밋 확인 → 바인딩 해제 → game 메모리 해제 → 객체 정리 순서다. Pause/Resume·미리보기 최소화는 바인딩을 해제하지 않는다. 객체 풀 반환/재사용은 generation을 바꾼다. 이전 객체/주 슬롯의 응답을 현재 객체에 적용하지 않는다.

| 접근 주체 | locked/access/open 처리 |
|---|---|
| 내부 게임 함수 | 모두 정상 수정 가능 |
| Controller 로컬 운영자 | 모두 정상 수정 가능. 타입/버전/대상 검증 유지 |
| 승인된 YouTube 명령의 게임 함수 | 명령 ACL이 호출 허용 판단. 호출 함수는 내부 쓰기 계약 적용 |
| 스트리밍 API의 직접 변수 수정 | 현재 모두 AccessControlNotImplemented. 공개 쓰기 허용 안 함 |

향후 직접 변수 ACL은 서비스가 확인한 계정 ID/권한을 사용한다. 요청자가 제출한 핸들·역할 문자열 자체는 인증 결과가 아니다. **slot은 더미가 아니며 실제 저장·복원·관리 대상**이다.

## 10. 구현 위치와 순서

| 위치 | 역할 |
|---|---|
| Engine/Data/DataRoutingMacros.h, DataContext.h | 표준 C++ 선언·등록·기본 분류 및 static_assert |
| Engine/Data/DataField.h, RoutedValue.h | native 멤버/컨테이너 proxy·바인딩·주 슬롯/generation |
| Engine/Data/MemoryStore.h, SqliteStore.h/.cpp | game Begin/End·SQLite·CAS·fallback·트랜잭션 |
| Engine/Data/DataValueCodec.h | 벡터·컨테이너·구조체·템플릿 코덱 |
| RiceController/Source/DataService, Shared/DataProtocol | 권위 SQL·슬롯 CRUD·reconnect ledger·데이터 IPC |
| Controller 슬롯 관리 UI | 방송 설정·타입에 맞는 데이터 편집 |
| Engine/Project/Workspace, Engine/Scene/Scene | Play/Stop·객체 수명·실행 준비 연결 |

구현 순서는 ① stable ID/코덱/native proxy ② C++ 등록 매크로/타입 검사 ③ game·stored SQLite/lifecycle ④ slot/주 슬롯 전환/관리 UI ⑤ global 매 접근 SQL/fallback/재접속 ⑥ 명령 커밋 연계/통합 검증이다. 현재 저장 기반은 Windows System32의 winsqlite3.dll을 명시적으로 로드하며 추가 DB 설치가 없다. [Microsoft 시스템 SQLite 설명](https://learn.microsoft.com/en-us/dotnet/standard/data/sqlite/custom-versions)

현재 저장 기반 테스트는 실제 SQLite 파일 재열기, WAL/CAS/동시 증가, game 종료 격리, 슬롯 교체와 일괄 schema 검증 rollback, global 최초 읽기/쓰기의 영속 decision/적용 marker, native 구조체·벡터·컨테이너 직접 변경을 검증한다. IPC UnknownCommit 큐/복구, 실행 lifecycle 전체 연결, 스키마 migration/백업 UI, 명령 성공 응답과 송출까지의 통합은 별도 완료 검증이 필요하다. 기반 테스트 통과를 전체 방송 기능 완료로 취급하지 않는다.

기존 제어 IPC 메시지 크기를 임의 컨테이너의 용량 보장으로 재사용하지 않는다. 데이터 메시지 최대 크기·큐 한도·분할 또는 거부 동작을 별도로 정의하고 양쪽에서 검사한다.

## 11. 수용 기준

| ID | 검증 | 기대 결과 |
|---|---|---|
| DR-T01 | 기본값·유효 네 조합·세 권한 | 일반 생략 build game locked, global 생략 global stored locked |
| DR-T02 | global game/slot/stream·문법 오류 | 지정 진단·위치 출력, 빌드 중단 |
| DR-T03 | 벡터·배열·목록·중첩 구조체·구체화 템플릿 | 코덱 있는 타입 왕복. 포인터/코덱 없음은 오류 |
| DR-T04 | native 매크로·잘못된 타입/분류 fixture | 전용 생성기 없이 C++20 컴파일 성공/실패 검사 |
| DR-T05 | 같은 BuildId의 두 game 실행 | 실행별 격리, 자기 세션만 해제 |
| DR-T06 | 슬롯 A의 G→H→G·Controller 재시작 | 게임별 분리, G 값 복원 |
| DR-T07 | 같은 인스턴스 A/B 출력, A 주 슬롯 | 양쪽 명령이 A 데이터 사용, B 복제 없음 |
| DR-T08 | A→B 주 슬롯 변경·B 없음/검증 실패 | 기본값 사용, A 복사 금지, 실패 시 A 유지 |
| DR-T09 | 주 슬롯 출력 중단·보조 활성 | 방송·데이터 문맥 유지, 자동 주 슬롯 변경 없음 |
| DR-T10 | locked CRUD·모두 초기화 | 운영자 편집 가능, 검증/generation, 늦은 값 부활 없음 |
| DR-T11 | global getter 두 번 | 매 논리 접근 SQL, 복사본 사용은 추가 접근 아님 |
| DR-T12 | 재접속 첫 읽기 | 권위 값이 로컬 덮어씀, dirty 자동 업로드 없음 |
| DR-T13 | 재접속 첫 쓰기/Increment | 로컬 후보 결과가 권위 덮어씀, 재시도 단 한 번 |
| DR-T14 | 첫 read/write 경쟁·다중 Player·응답 유실 | 영속 최초 결정, 직렬화, UnknownCommit 중복 없음 |
| DR-T15 | 통상 CAS/Increment·uint64 경계 | 충돌 보고, 숫자 손실/overflow/중복 없음 |
| DR-T16 | DB busy/디스크 부족/손상/강제 종료 | 미커밋 성공 표시 없음, 커밋값 복원 |
| DR-T17 | WAL backup·migration 실패/복원 | 일관 백업, 원본 유지, 타입/버전 확인 |
| DR-T18 | 이름/설명·BuildRevision 변경 | DataId/값 유지, 사용자 설명 우선 |
| DR-T19 | 중요한 명령 저장 실패/성공 | 커밋 전 성공 응답 없음, LocalCommitted 구분 |
| DR-T20 | 직접 원격 수정 vs 내부/운영자 | 원격 ACL 더미는 거부, 내부/운영자 정상 |
| DR-T21 | 독립 slot·global cold fallback | slot 문맥 오류, global 기본값/UnseededFallback 명시 |

## 12. 확정 사항과 구현 경계

SQLite, slot 영속성, 주 슬롯 문맥, slot CRUD의 locked 우회, global 임시 수명 금지, 매 접근 SQL, 재접속 첫 연산, 데이터 전용 클래스, 복합 타입 지원을 이번 버전에 반영했다. 이전의 global game 종료 시점·stream 더미·기본 타입 한정·프레임별 global 스냅샷·Controller 없으면 global 전면 실패 안은 폐기한다.

추천안 10~13 수락에 따라 stable DataId/ObjectKey, 쓰기 소유자 및 원자 연산, 커밋 확인 후 중요한 명령 성공, 엔진 내부 쓰기와 외부 ACL 분리를 채택한다.

요청의 빈 부분은 다음 기본 정책으로 구체화한다. 구현을 추가 승인 대기 상태로 만드는 항목은 아니다.

- 주 슬롯은 안전한 실행 경계에서 바꾸며 실패 시 기존 문맥을 유지한다. 주 슬롯 출력 종료만으로 자동 전환하지 않는다.
- 이름 편집은 표시 이름이다. C++ 식별자/DataId 변경은 migration 또는 리팩터링이다.
- 모두 초기화는 선택 게임 종류가 기본 범위이며 슬롯 전체 범위는 UI에서 명시한다.
- global 최초 판정은 키별이다. 자동 관리 조회는 판정을 소비하지 않으며 cold fallback은 미시드 상태를 표시한다.
- Field<T>의 native proxy로 일반 목록/등록 멤버 직접 변경을 제공한다. 전용 Rice 언어나 소스 생성기는 사용하지 않는다. 순수 int ABI, raw 참조/iterator, 등록하지 않은 임의 메서드의 호환은 보장하지 않는다.
- 직접 원격 변수 ACL은 후속 구현이다. YouTube 명령 실행 권한과 운영자 편집은 별도 구현한다.

IPC 크기·busy/flush deadline·컨테이너 한도는 기능 의미를 바꾸지 않는 튜닝 설정이며 실제 부하 검증으로 정한다. SQL 대기가 송출을 막지 않는지는 필수 통합 수용 조건이다.
