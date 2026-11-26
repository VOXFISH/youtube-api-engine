# Rice 엔진 함수 명세서

이 문서는 현재 공개 API의 동작 계약이다. 모든 씬·컴포넌트·GUI·FMOD 호출은 메인 스레드에서 사용한다. 헤더는 Engine 아래에 있으며 C++20, RiceCore, RiceBuildOptions, glm::glm을 연결한다. 프로젝트 기능을 쓸 때는 Project/Workspace.h, GUI에는 Editor/SceneInspector.h를 포함한다.

## 객체·씬·생명 주기 — Scene/Scene.h

| API | 입력 / 반환 / 계약 |
|---|---|
| Scene::CreateGameObject(name, id=0) → GameObject& | 씬 소유 객체 생성. id=0은 자동 발급, 명시 ID 중복은 예외. 반환 참조는 삭제·씬 교체 전까지 유효. |
| Scene::Find(EntityID) → GameObject* | 없거나 삭제 예약이면 nullptr. Undo·Load·Play·Stop 이후에는 ID로 재조회한다. |
| Scene::Destroy(id) | 대상과 모든 자손을 삭제 예약. 이 호출 중 메모리는 해제하지 않는다. |
| Scene::Flush() | 삭제 예약 컴포넌트·객체 해제. 콜백 순회가 끝난 프레임 경계에서 호출한다. |
| Scene::GetObjects()/GetObjectCount() | 소유 목록의 const 참조 / 저장된 객체 수. Flush 전 예약 객체가 목록에 남을 수 있다. |
| `Scene::EachComponent<T>(visit)` | 씬의 타입별 sparse set에서 `T`와 그 파생 컴포넌트를 `T&`로 순회한다. 순서는 보장하지 않는다. 비활성 객체·컴포넌트도 포함하며, 제거·삭제 예약 항목은 건너뛴다. 순회 중 추가한 컴포넌트는 다음 순회부터 방문한다. |
| Scene::Update(float dt) | 유한한 0 이상의 초. 잘못된 dt는 invalid_argument. 콜백 예외는 호출자로 전파. |
| Scene::Render() | 활성 사용자 컴포넌트 Render 콜백 호출. 내장 메시 렌더링은 SceneRenderer가 수행. |
| GameObject::GetID()/GetScene() | 객체 ID / 소유 씬. 독립 생성한 객체에서 GetScene은 예외. |
| GameObject::AddComponent<T>(args...) → T& | 타입당 한 개. 기존 타입은 기존 인스턴스 반환. 새 컴포넌트에 Awake 즉시 호출. Transform은 기존 필수 값 반환. |
| GameObject::GetComponent<T>() → T* | 없거나 제거 예약이면 nullptr. |
| GameObject::RemoveComponent<T>() | 제거 예약. Transform 제거는 컴파일 오류. |
| GameObject::SetActive(bool)/IsActive() | 로컬 활성 값 설정 / 부모 활성 상태와 삭제 예약까지 반영한 결과. |
| GameObject::SetParent(GameObject*) | nullptr은 루트. 로컬 Transform 유지. 순환·다른 씬 부모는 예외. |
| GameObject::GetParent()/GetWorldMatrix() | 부모 포인터 / 부모 행렬 × 로컬 행렬. |
| GameObject::TranslateWorld(Vector3 delta) | 월드 이동량을 부모 공간으로 변환해 로컬 위치에 반영. 특이 부모 행렬은 예외. |
| Transform::GetMatrix()/Translate(offset) | T×R×S 로컬 행렬 / 로컬 위치 증가. rotation은 도 단위. |
| Component::GetGameObject() | 소유 객체 참조. 부착 전 사용하면 예외. |

생명 주기: AddComponent → Awake → 첫 활성 Update에서 OnEnable → Start(1회) → Update. 이후 활성 상태 변경은 다음 Update에서 OnEnable/OnDisable로 전달한다. 제거·삭제 때 OnDisable(활성화되었을 때)와 OnDestroy를 호출한다. 소멸자에서 던진 생명 주기 예외는 전파하지 않는다. 콜백 안에서는 Destroy/RemoveComponent를 사용하고 Flush를 직접 호출하지 않는다.

객체 기반 컴포넌트 저장소이며 SoA/archetype 레지스트리가 아니다. 직렬화할 커스텀 컴포넌트는 ComponentRegistry에 등록해야 한다.

## 렌더링 — Scene/Components.h

| API / 데이터 | 계약 |
|---|---|
| MeshRenderer::color | 큐브 재질 색상. |
| SpriteRenderer::asset/color/texture/uvRect | 영속 에셋 ID·색상과 런타임 텍스처·UV 사각형. Workspace가 ID를 해석해 texture를 설정. |
| SpriteRenderer::LoadImage(path) → bool | 저수준 직접 파일 로딩. 유효 GL 컨텍스트 필요. 실패 시 이전 texture 유지. asset ID를 자동 등록하지 않으므로 프로젝트 작업에서는 Import + asset을 사용. |
| Light::color/intensity/direction/type/range | 점광원/방향광 데이터. type=0 Point(기본), 1 Directional. range=10 기본. 최대 8개. 음수 세기는 렌더링 시 0, 영벡터 방향은 (0,-1,0)으로 보정. |
| SceneRenderer::SceneRenderer() | GL 버퍼·GLSL 프로그램 생성. 컨텍스트가 살아 있는 동안 소멸해야 함. |
| SceneRenderer::Render(Scene&, Camera&) | 활성 Mesh/Sprite 렌더링. 계층 월드 행렬 사용. 0에 가까운 로컬 크기는 건너뜀. |
| SceneRenderer::SetCubeInstancingEnabled(bool) | 기본 true. 같은 텍스처·재질을 쓰는 연속 기본 큐브의 instanced draw를 활성화. false는 개별 draw 비교 경로. |
| SceneRenderer::GetRenderStats() | 마지막 Render의 mesh/sprite drawCalls, instancedDrawCalls, instancedInstances. 환경·그리드와 반사 캡처 내부 패스는 제외. |
| Renderer::BeginScene/Submit/EndScene | 카메라 행렬 설정 / 셰이더·VAO·Transform 인덱스 렌더 / 종료. |
| Shader::Create(vertexSource, fragmentSource) | 호출자 소유 포인터 반환. 컴파일·링크 실패는 runtime_error. |
| Texture2D::Create(path) | shared_ptr 반환. 실패 판별에는 GetWidth()==0도 확인. |
| PerspectiveCamera::SetPerspective/SetPosition/LookAtObj | 원근 투영, 위치, 주시점 설정. |

렌더러는 SpriteAnimator와 함께 UV를 사용한다. 알파 cutout을 제공하며 일반 반투명 블렌딩은 없다.

큐브 인스턴스는 월드 mat4와 색상 vec4를 80바이트로 저장하며 매 렌더 패스마다 재사용 VBO를 orphan/upload한다. 별도 VAO가 기존 정적 큐브 버퍼를 공유하고 속성 3–6(행렬), 7(색상)에 divisor=1을 지정한다. 호환 재질의 연속 큐브를 `glDrawElementsInstanced` 한 번으로 제출하고 다른 렌더 유형 전에는 배치를 비운다. 반사 수신·면별 재질·mesh와 sprite 동시 사용은 개별 경로로 처리한다. `RenderShape`와 일반 경로는 instancing 셰이더 상태를 명시적으로 초기화한다.

## 물리·게임플레이 — Scene/Gameplay.h

| API / 데이터 | 계약 |
|---|---|
| BoxCollider::center/size/isTrigger/overlapping | 로컬 중심·크기, Trigger, 마지막 검사 겹침 결과. |
| BoxCollider::GetBounds() → pair<Vector3,Vector3> | 8개 꼭짓점의 월드 최소/최대 AABB. 음수 size는 절댓값. |
| SphereCollider::center/radius/isTrigger/overlapping | 로컬 중심과 반지름. 기본 반지름은 0.5이고 월드 변환에서 가장 긴 축의 배율을 적용한다. |
| PolygonCollider::center/isTrigger/overlapping | 같은 오브젝트의 MeshRenderer 삼각형으로 충돌 표면을 구성한다. center는 메시의 로컬 좌표에 더하는 오프셋이다. |
| CollisionSystem::Update(Scene&) → size_t | 단순 겹침 쌍 수. 이벤트·반응 없는 기존 조회 API. |
| Rigidbody::velocity/mass/restitution/useGravity/kinematic | Inspector의 Physics 컴포넌트. Body type의 Dynamic은 kinematic=false, Kinematic은 true에 대응한다. 질량은 계산 시 최소 0.001. |
| PhysicsSystem::Update(Scene&, dt) | 내부 1/60초 누적 스텝. 프레임당 입력 시간 최대 0.1초. 잘못된 dt는 예외. 충돌 모양별 접촉 판정 후 침투와 반발을 처리한다. |
| PhysicsSystem::Reset() | 누적 시간·이전 접촉 비우기. 씬 교체 때 필요. |
| OnCollisionEnter/Stay/Exit(EntityID) | 일반 접촉 시작·유지·종료 콜백. 상대 삭제 후 Find는 nullptr일 수 있음. |
| OnTriggerEnter/Stay/Exit(EntityID) | 한쪽 Collider가 Trigger인 접촉 콜백. 물리 반응 없음. |
| SpriteAnimator::Update(dt) | columns/rows/frames/fps/loop/playing으로 frame과 clock 갱신. Workspace가 Sprite UV에 반영. |
| PlayerController::speed | Workspace의 이동·점프 처리에 사용. 자체 Update만 호출해서는 입력 이동이 진행되지 않음. |
| InputActions::Bind(name,key) | 액션 추가·재바인딩. 비어 있는 이름 또는 32~348 밖 키는 예외. |
| InputActions::BeginFrame(captured=false) | GLFW 입력 샘플링 및 이전 상태 보관. 유효 Application 창 필요. |
| InputActions::Held/Pressed/Released(name) → bool | 현재 상태 / 이번 프레임 눌림 / 해제. 없는 액션은 false. |
| InputActions::Axis(negative,positive) → float | 양수 액션 - 음수 액션, -1/0/1. |

PhysicsSystem은 상태를 가지므로 씬마다 별도 인스턴스를 사용한다. 공간 인덱스로 충돌 후보를 좁힌 뒤 고정 물리 스텝에서 접촉을 판정한다. 빠른 물체는 두 스텝 사이에 얇은 표면을 통과할 수 있다.

## FMOD — Audio/Audio.h

| API | 계약 |
|---|---|
| AudioSystem(bool silent=false) | FMOD Core 시스템 생성, 128채널 초기화. silent는 FMOD NOSOUND 출력. 실패 시 runtime_error. |
| AudioSystem::Play(path,loop,volume) → FMOD::Channel* | 파일 경로별 Sound 캐시 후 채널 재생. volume은 0~1 보정. |
| AudioSystem::Update()/StopAll() | 매 프레임 갱신 / 마스터 채널 정지. |
| AudioSource::clip/loop/playOnStart/volume | 영속 에셋 ID·반복·자동 시작·볼륨. |
| AudioSource::Play(system,path)/Stop()/Pause(bool) | 이전 채널을 정지하고 재생 / 정지 / 일시정지·재개. |
| AudioSource::Sync() | 볼륨·반복을 채널에 적용. 종료·회수된 핸들은 비움. |
| AudioSource::IsPlaying() → bool | 유효 채널 재생 여부. |

AudioSystem은 모든 AudioSource보다 오래 살아 있어야 한다. Workspace가 이 순서를 보장한다. Studio bank/event가 아닌 Core 파일 재생 경로다. 임의 FMOD 채널 포인터를 따로 보관하지 않는다.

## 에셋·저장 — Project/Assets.h, SceneCodec.h

| API | 계약 |
|---|---|
| AssetDatabase(root) | 에셋 폴더 생성 및 스캔. 누락된 .meta 생성. 중복 ID는 오류. |
| Import(source) → string | 원본 파일을 루트로 복사하고 ID 반환. 같은 파일명 존재 시 거부. |
| Resolve(id) → filesystem::path | ID에 대응하는 절대 경로. 없는 ID는 예외. |
| Rename(id,relativePath) | 파일과 .meta 함께 이동. 절대·상위 탈출·기존 대상 경로 거부. |
| Refresh()/Entries()/Root() | 재스캔 / 목록 / 에셋 루트. |
| Texture(id) → Ref<Texture2D> | 약한 참조 캐시로 이미지 공유. GL 컨텍스트 필요. 실패는 예외. |
| ComponentRegistry::Register<T>(name,fields) | 타입 생성·조회·제거 및 속성 방문 함수를 등록. 이름 중복 거부. |
| PropertyVisitor(label,Property) | bool/int/float/string/Vector2/Vector3/Vector4 포인터 지원. Enabled는 자동 등록. |
| SceneCodec(registry)::Encode(Scene&) → string | 버전 3 텍스트 생성 (버전 1·2 읽기 호환). 미등록 컴포넌트는 거부하여 조용한 데이터 손실 방지. |
| Decode(text) → Scope<Scene> | 최대 16 MiB/10,000 객체. 문법·버전·ID·계층·속성 검증 후 새 씬 반환. 실패 시 기존 씬에 영향 없음. |
| Save(scene,path)/Load(path) | 텍스트 저장 / 읽고 Decode. 저장 전후 .tmp/.bak 파일 사용. |
| Duplicate(scene,id) → GameObject& | 새 ID로 속성·컴포넌트 복제. 부모 유지, 자손 트리는 복제하지 않음. |
| AtomicWrite(path,text)/ReadFile(path) | 실패 시 예외. AtomicWrite는 백업 보관 방식이며 OS 크래시까지 보장하는 DB 트랜잭션은 아님. |

등록 이름·속성 순서는 파일 스키마의 일부다. 현재 자동 스키마 마이그레이션은 없다. std::function, GPU 자원, FMOD 채널, 스크립트 미등록 필드는 저장하지 않는다.

## 편집·프로젝트 — Project/Workspace.h

| API | 계약 |
|---|---|
| EditorSession::Edit(fn) | 변경 전 스냅샷, 사용자 변경, Flush, Undo 기록. |
| Record(before)/Undo()/Redo() | 최대 100개 장면 스냅샷. 플레이 중 기록·Undo/Redo 무시. |
| Play()/Stop() | 실행 씬 복제 / 편집 씬 복원. 참조 포인터는 무효화되므로 ID로 재조회. |
| Replace(Scope<Scene>)/Load(path) | 씬 교체와 히스토리 초기화. |
| Workspace::Save() | 프로젝트의 입력 바인딩과 씬을 이름.rice 한 파일에 저장. 이전 프로젝트는 저장할 때 v3로 변환. |
| Project::ActiveScene()/FilePath() | 프로젝트 내부의 활성 씬 이름 / 저장된 프로젝트 파일 경로. |
| Project::SceneFiles()/Workspace::SwitchScene(relative) | 프로젝트 내부 씬 목록 조회 및 활성 씬 전환·저장. |
| Project::Export(scene,registry,binaries,destination) | 프로젝트 밖의 새 EXE에 플레이어·필요한 DLL·애셋·프리팹·씬·배포용 스크립트 모듈을 패키징. |
| Workspace(root,silent,registerTypes) | 오디오·레지스트리·에디터·프로젝트 생성. registerTypes는 프로젝트 로딩 전에 실행. |
| Workspace::Save/Open/New | 편집 씬·프로젝트 저장 / 검증 후 프로젝트 전환 / 새 폴더에 빈 프로젝트 생성. |
| Workspace::SwitchScene(relative)/NewScene(name,duplicate=false) | 편집 중 현재 씬 자동 저장 후 씬 전환 / 빈 씬 또는 현재 씬 복사 생성. 실패 시 로드 전 검증하며 Undo 이력은 전환 시 초기화. |
| Workspace::Play/Stop | EditorSession 모드 전환과 물리·오디오·이미지·Prefab 풀 초기화. Benchmark의 Play 프레임 시작/정지. |
| Workspace::Update(dt,captureKeyboard) | 입력, 사용자 Update, 물리, 이미지·애니메이션, 오디오, Flush. |
| Workspace::PlayAudio(AudioSource&) | 에셋 ID 해석 후 FMOD 재생. |
| Workspace::Log(text)/Report(exception) | 최근 200개 로그 / 중복 예외 메시지 억제. |
| Workspace::actions | UI 액션 이름 → 함수. 프로젝트 로딩 후, Play 전에 등록. |

기본 프레임 순서는 Workspace::Update → SceneRenderer::Render → Scene::Render → ImGui 프레임 및 GUI다. GameUI는 SceneInspector 내부에서도 호출되므로 중복 호출하지 않는다.

## Prefab — Project/Prefab.h

`Workspace::SavePrefab(root,name)`은 선택 루트와 자손의 등록된 컴포넌트·속성·계층을 `Assets/Prefabs/<name>.prefab`에 저장한다. 외부 부모는 분리하고 내부 부모·그룹은 새 인스턴스 ID로 재연결한다. 편집 중 `Workspace::InstantiatePrefab(name,position)`은 영속 씬 객체를 만들고 Play 중에는 풀을 사용한다.

게임 스크립트는 `Start`·`Update` 등 플레이 중 콜백에서 `Scene::InstantiatePrefab(name,position)`, `Scene::PrewarmPrefab(name,count)`, `Scene::ReleasePrefab(rootID)`를 사용한다. `Awake`는 직렬화 복원 중 호출되므로 런타임 Prefab 연결 전에 실행될 수 있다.

- `PrefabPool::Prewarm(scene,asset,count)`는 사용 가능한 루트 인스턴스를 count개 이상 준비한다.
- `Instantiate`는 같은 Prefab의 반환된 객체와 컴포넌트를 재사용한다. 저장 속성·변환을 복원하고 `OnPoolAcquire` → `OnEnable` 순으로 알린다. `Start`는 컴포넌트당 한 번이다.
- `Release`는 객체를 비활성화하고 `OnDisable` → `OnPoolRelease`를 호출한다. 중복 반환은 false다. 비직렬화 사용자 상태는 이 콜백에서 직접 초기화한다.
- `Root(scene,memberID)`는 자식 선택을 풀 소유 루트 ID로 해석하며 해당하지 않으면 0이다.
- `Clear(scene)`는 해당 씬의 풀 객체를 제거한다. 씬 인스턴스 ID로 다른 씬이나 오래된 핸들에 접근하지 않는다.

## Benchmark — Rice/Benchmark.h

`BeginPlay` 이후 다음 전체 프레임부터 1로 세며 `EndPlay` 후에는 0이다. `Schedule(start,frames,intervals)`는 Play 프레임만 선택하고 편집 중에도 예약할 수 있다. Stop은 미완료 측정을 취소하며 완료 결과는 보존한다.

`Initialize`/`BenchmarkSession`은 진단을 준비하며, `Schedule`이 선택한 프레임에서만 `RICE_BENCHMARK_FUNCTION()`이 호출을 수집한다. 현재 프레임에서 예약하면 예약 이후부터 기록하며, 과거 호출을 소급 수집하지 않는다. 비활성 계측은 시계 조회·버퍼 할당·잠금 없이 종료한다. `SetThreadName(name)`으로 작업 스레드 이름을 지정할 수 있다. 결과와 로그는 스레드 ID·호출 ID를 포함하고, 수집 중 시작한 호출이 다음 수집 프레임까지 이어지면 같은 호출 ID의 연속 구간으로 나눈다.

별도 워커가 버전·메타데이터 ID를 포함한 바이너리 파일을 쓰고, 진단 종료 후 TSV로 변환한다. `BenchmarkStatus`의 `recording`은 현재 수집 여부, `finalizing`은 로그 내보내기 여부이며 `writtenCalls`, `binaryBytes`, `binaryFile`, `textFile`로 결과 저장 상태를 확인한다. Benchmark 창은 스레드별 독립 접기/펼치기를 제공하며 Rate는 모든 스레드의 자체 시간 합을 분모로 사용한다.

## GUI — Editor/SceneInspector.h

| API | 계약 |
|---|---|
| SceneInspector::Draw(workspace,binaries,render) | ImGui 프레임 안에서 호출. 계층·속성·컴포넌트·프로젝트·에셋·입력·진단 창과 게임 UI 제공. |
| GameUI::Draw(workspace) | 독립 플레이어용 UI. UIText와 UIButton 순회, 플레이 중에만 버튼 액션 실행. |
| EditString(label,string&)/DrawProperty(label,Property) | 문자열 또는 등록 속성을 편집하는 ImGui 헬퍼. |

위치·크기는 픽셀 단위다. UIText `{score}` 치환 및 Increment/Quit는 샘플 게임 규약이며 별도의 게임 로직으로 교체할 수 있다.

## 런타임

Application::Run은 초기화·입력·업데이트·렌더·GUI·버퍼 교환·종료 순서로 실행한다. Close는 다음 반복 종료를 요청한다. OnInit/OnUpdate/OnRender/OnShutDown을 재정의한다. 창과 GL 자원 생명 주기를 준수하며, 프레임 시작 시간을 초기화하여 초기화 지연이 첫 DeltaTime에 포함되지 않도록 한다.

LayerStack은 레이어 소유권을 가진다. PopLayer/PopOverlay는 OnDetach 후 delete한다. 기존 iterator 대신 인덱스를 사용하여 overlay 추가 후 iterator 무효화 문제를 방지한다.


## 도킹 뷰 API

- `SceneInspector::Draw(workspace, binaries, render)`는 DockSpace와 Hierarchy/Inspector/Scene/Game/Project/Console/Input 패널을 생성한다. render는 `Matrix4(int width, int height, bool game)` 콜백이다. 호출 시 해당 패널 framebuffer가 바인딩되어 있으며 콜백에서 그린 결과가 텍스처로 표시된다. 보이는 뷰마다 렌더 콜백이 실행될 수 있으므로 콜백 안에서 시뮬레이션 시간을 진행시키지 않는다.
- `SceneInspector::CapturesGameInput()`은 Game 뷰가 닫혀 있거나 이전 GUI 프레임에 포커스가 없었으면 true다. 애플리케이션 입력 처리의 captureKeyboard 조건으로 사용한다.
- `GameUI::DrawContents(workspace, origin)`은 현재 ImGui 윈도우의 지정한 화면 좌표를 게임 UI 원점으로 사용한다. Game 패널에서는 콘텐츠 사각형으로 클리핑한다.
- `ViewportTarget::Render(width, height, draw)`는 OpenGL 3.3 framebuffer와 RGBA8 컬러 텍스처, depth/stencil renderbuffer를 생성·재사용하고 텍스처 ID를 반환한다. 크기 변경 시 재할당하며 GL 최대 텍스처 크기로 제한한다. draw 이후 기존 framebuffer와 viewport 등을 복원한다. 완전하지 않은 framebuffer는 예외를 던진다.
- `ViewportTarget`은 복사할 수 없으며 유효한 GL 컨텍스트가 유지된 상태에서 소멸해야 한다.

편집기 프레임에서는 Workspace::Update 이후 ImGui 프레임 안에서 각 뷰의 렌더 콜백을 호출한다. 독립 플레이어는 기존 OnRender → 전체 창 GameUI 경로를 유지한다. 레이아웃은 실행 파일 옆 EditorLayout.ini에서 불러오고 자동/수동 저장한다. Dear ImGui의 멀티 OS viewport는 비활성화되어 있다.


## 통합 로그 API — Rice/Debug.h

- `Debug::Init()`은 엔진·게임 로거를 같은 메모리 sink에 연결한다. 반복 호출은 안전하며 기본 spdlog 로거도 APP으로 설정한다.
- `Debug::GetMessages() → vector<LogEntry>`는 잠금 안에서 로그 스냅샷을 복사한다. LogEntry에는 증가하는 sequence, level, 시간·수준·출처를 포함하는 text가 있다. UI 객체를 로그 스레드에서 직접 호출하지 않는다.
- `Debug::ClearMessages()`는 버퍼를 비운다. sequence는 초기화하지 않는다.
- `Debug::MessageCapacity`는 2,000이다. 오래된 항목부터 제거하고 개별 메시지는 16 KiB 이후 잘라낸다.
- `Workspace::Log(text, level=info)`도 같은 sink에 기록한다. 기존 workspace.logs는 최근 200개 Workspace 메시지의 호환용 목록이며 Console은 Debug 스냅샷을 표시한다.
- `RICE_CORE_INFO("{}", value)`, `RICE_CORE_WARNING`, `RICE_CORE_ERROR`, `RICE_LOG`, `RICE_ERROR` 등 기존 매크로를 그대로 사용한다. Debug::Init 후 호출한다.

테스트는 엔진/게임/기본 spdlog 경로, 수준 보존, 다중 스레드 기록, 보관 상한, 긴 메시지 절단, Clear를 검사한다. --smoke는 OpenGL 초기화 로그가 동일 버퍼에 들어왔는지도 확인한다.


## TransformGizmo — Editor/TransformGizmo.h, GizmoMath.h

- SceneInspector의 render 콜백은 그 프레임에 실제 렌더링한 카메라의 ViewProjection 행렬을 반환해야 한다. Scene 선택과 핸들 투영이 이 행렬을 사용한다.
- `TransformGizmo::Draw(editorSession, origin, size, viewProjection, target, canvasHovered)`은 Scene 콘텐츠 위에 박스·핸들을 그리며 클릭과 드래그를 처리한다. 활성 ImGui 프레임과 마우스 좌/우 버튼을 받는 SceneCanvas 항목이 필요하다.
- `Active()`는 진행 중인 드래그 여부, `Cancel(workspace)`는 시작 Transform 복원이다. 드래그 종료 때 EditorSession::Record를 한 번 호출한다.
- `GizmoMath::RotationSnap::Reset(angle)/Apply(rawAngle)`은 90도 배수의 ±5도 고정과 범위 이탈 후 역방향 우회 상태를 관리한다. 입력은 도 단위이며 한 드래그 동안 같은 인스턴스를 사용한다.
- `ScreenRay(pixel, origin, size, vp)`는 뷰 좌표를 월드 광선으로 변환한다. `Project(world, origin, size, vp)`는 화면 좌표·깊이를 반환하며 카메라 뒤/클립 영역 밖은 nullopt다.
- `Hit(ray, world, low, high)`는 변환된 로컬 박스의 광선 교차 거리, `Pick(scene, ray)`는 가장 가까운 활성 객체 ID 또는 0을 반환한다.
- `ScaleSelected(target,start,mask,factor)`는 mask의 축만 객체 원점을 유지하며 배율 변경한다. factor는 최소 0.02다.
- `RotateFace(target,start,pivot,axis,degrees)`는 로컬 면 중심 pivot을 부모 좌표에서 고정하며 로컬 축 회전을 적용한다. Euler 표시는 드래그 기준과 가장 가까운 동치 표현을 사용한다.


## 편집기 클립보드 — Editor/EditorClipboard.h

- `CopyObject(scene,id,registry)`는 선택 객체·자식의 이름/활성/Transform/등록 컴포넌트와 월드 위치를 스냅샷으로 보관한다. 원본 포인터를 보관하지 않는다.
- `CutObject(editor,registry)`와 `DeleteObject(editor)`는 선택 객체·자손을 제거하며 EditorSession::Edit로 Undo를 기록한다. 플레이 중 또는 선택이 없으면 아무 작업도 하지 않는다.
- `PasteObject(editor,registry) → EntityID`는 새 객체와 자손을 생성하고 내부 부모 관계를 연결하며 복사 시점의 월드 위치를 맞춘다. 붙여넣은 루트를 선택한다. 클립보드가 비었거나 플레이 중이면 0이다. 생성 중 예외가 발생하면 생성한 객체들을 제거한다.
- `CopyComponent(object,typeName,registry)`는 지정 타입의 Enabled와 모든 등록 속성을 복사한다. typeName이 Transform이면 필수 Transform 값을 복사한다.
- `CopyAllComponents(object,registry)`는 Transform과 모든 등록 컴포넌트 속성을 보관한다. 미등록 컴포넌트는 예외로 거부한다.
- `PasteComponents(object,registry)`는 보관한 속성을 임시 객체에서 검증한 뒤 적용한다. 기존 타입에는 값만 덮어쓰고 없는 타입은 부착한다. 이 저수준 API는 자체 Undo 기록을 만들지 않으므로 코드에서는 EditorSession::Edit 안에서 호출한다. GUI는 프레임 편집 스냅샷으로 기록한다.
- `HasObjects()/HasComponents()`는 각 클립보드의 데이터 존재 여부를 반환한다.
- `Scene::GetInstanceID()`는 씬의 메모리 수명별 ID다. 파일 객체 ID와 별개이며 클립보드가 다른 씬의 우연히 같은 ID를 부모로 연결하지 않도록 사용한다.

GUI는 텍스트/활성 위젯/팝업/핸들 조작 및 플레이 중 단축키를 억제하고, Ctrl+Z/Y/X/C/V와 Delete/Backspace를 위 API에 전달한다. 컴포넌트 헤더 우클릭 메뉴도 동일한 API를 사용한다.

## 공유 선택 / 계층 편집 API

EditorSession은 공유 선택 집합 selection, Inspector 대상 selected를 제공합니다.

| 함수 | 명세 |
| --- | --- |
| SyncSelection() | 삭제된 ID 제거, 기존 단일 선택 API와 집합 동기화 |
| IsSelected(id) | 선택 여부 반환 |
| Select(id, additive=false) | 단일/추가 선택, 자식 조작 모드 해제 |
| SelectChildren(id) | 대상과 표시 계층의 재귀 자손 선택 |
| DeleteSelection() | 선택 객체와 자손을 하나의 Undo 기록으로 삭제. 실행 중 무시 |
| GameObject::GetWorldMatrix() | 부모 월드 행렬과 로컬 Transform 재귀 합성 |

`SceneInspector::DrawHierarchy`는 `Scene::GetHierarchyRevision()`이 바뀔 때만 `HierarchyCache`의 부모·자식 목록을 재구축합니다. 선택·펼침 변경 시에는 평탄화된 표시 목록만 갱신합니다. `HierarchyNode`는 `ImGuiListClipper`가 선택한 화면 내 행과 활성 팝업·드래그 행만 그립니다. `ObjectMenu`는 객체 목록 변경 작업을 순회 종료 후 실행합니다. TransformGizmo::Draw의 target ID는 여러 객체의 핸들 표시/조작에 사용하며 consumed는 핸들과 컨텍스트 메뉴의 입력 중복을 방지합니다.


## 그룹 API

| API | 명세 |
| --- | --- |
| EditorGroups::Create(editor, ids, forcedParent=0) | 최상위 선택 멤버들을 그룹으로 결합. 월드 경계 중앙에 그룹 생성, 월드 변환 보존, 새 ID 반환. 빈 선택/플레이 중은 0 |
| EditorGroups::Ungroup(editor, id) | 그룹만 제거, 멤버의 월드 행렬과 원래 부모/이전 그룹 보존. 일반 객체에는 아무 작업도 하지 않음 |
| EditorGroups::ControlChildren(editor, id) | 직접 자식으로 그룹 생성, 해당 객체 아래 배치 |
| EditorGroups::Reparent(object, parent) | 월드 변환을 유지하며 재배치. 대상이 그룹이면 그룹 링크 변경. UI는 EditorSession::Edit 안에서 호출 |
| GameObject::isGroup | 그룹 오브젝트 식별. 일반 컴포넌트 부착 가능 |
| GetParent() / GetGroup() / GetHierarchyParent() | 원래 부모 / 그룹 링크 / Hierarchy에 표시할 부모 |
| SetGroup(group, preserveWorld=true) | 그룹 연결 및 바인딩 행렬 갱신. 동일 씬, 그룹 타입, 순환 참조 검증 |
| GetParentMatrix() / GetWorldMatrix() | 원래 부모와 그룹의 결합 행렬 / 최종 월드 행렬. 공통 조상 중복 제거 |
| SetWorldMatrix(matrix) | 유효 부모 공간으로 환산해 TRS와 affine 보정 행렬 설정 |
| Transform::SetMatrix(matrix) | TRS 분해 및 잔여 shear 보존. 특이 행렬 거부 |
| GizmoMath::SpaceBounds(object, space) | 중첩 그룹의 실제 멤버 경계들을 지정 공간에서 합산. 단위 행렬이면 월드 AABB |

그룹 조작은 멤버의 월드 변환으로 전파되며 로컬 숫자 직접 대입과 구분됩니다. 그룹을 다른 부모로 이동할 때는 EditorGroups::Reparent를 사용해 바인딩을 함께 갱신합니다. SceneCodec 버전 2에는 그룹 ID, 이전 그룹 ID, 그룹 바인딩, 공통 조상 바인딩, affine 보정 행렬을 기록합니다. EditorClipboard는 그룹 멤버 링크를 새 ID로 치환하고 의존 순서에 따라 월드 행렬을 복원합니다.


## 편집 전용 Transform 프리뷰와 입력

- `EditorSession::PreviewTransform(id, transform)`은 편집 씬의 Transform만 적용하고 `previewRevision`을 증가시킨다. 실행 중이거나 대상이 없으면 false를 반환한다. 게임 lifecycle/물리/오디오 업데이트를 호출하지 않는다. Undo 기록은 드래그 완료 시 한 번만 수행한다.
- `EditorSession::Snapshot()`은 현재 편집 씬의 직렬화 스냅샷을 반환한다.
- `TransformGizmo::BeginFrame()`은 여러 선택 핸들의 공통 입력 소유 상태를 갱신한다. `Draw`에는 InvisibleButton 직후 저장한 `IsItemHovered()`를 전달한다. 활성 Canvas로 차단되는 `IsWindowHovered()`는 핸들 시작 판정에 사용하지 않는다.
- `consumed`는 핸들 우클릭에서 버튼 해제 프레임까지 유지한다. Scene의 객체 선택/메뉴는 Active 또는 consumed일 때 건너뛴다.
- `PlaneIntersection(ray, point, normal)`은 광선과 평면의 교차점을 반환한다. 거의 평행하거나 카메라 뒤쪽인 경우 빈 optional을 반환한다.
- `FaceNormal(world, axis)`는 역전치 행렬로 면 법선을 계산하여 회전·비균일 스케일·shear를 지원한다.
- `HandleRadius(face)`는 표시 반경 3.5/2.5px, `HandleHitRadius(face)`는 클릭 반경 10/8px다.
- SceneInspector는 프리뷰 revision이 바뀌면 해당 View의 렌더 타깃을 즉시 갱신한다. 게임 Update나 물리 step은 실행하지 않는다.


## 이동 면 접촉 보정 API

- `GizmoMath::SurfaceSnap::Reset(scene, selected, planeNormal)`은 제스처 시작 시 이동 객체 및 종속 자손의 월드 경계 면과 외부 대상 면을 수집한다. 반대 방향의 평행 면, 드래그 평면 안에서 접촉 가능한 면 쌍만 후보로 만든다.
- `Apply(rawWorldTranslation, projectToScreen)`은 이전 원시 이동량과 현재 이동량으로 접촉 진입을 검출하고 보정된 월드 이동량을 반환한다. 보정 결과를 다음 입력으로 사용하지 않는다. 원시 이동 위치와 접촉 보정 위치의 화면상 거리 0~HoldPixels 범위에서 고정, 범위 이탈 후 역방향 접근은 bypass 상태로 처리한다.
- `HoldPixels = 10.f`는 View 마우스 좌표의 픽셀 단위다. 투영 콜백은 이동량을 현재 카메라/뷰포트의 화면 좌표로 변환하고 투영 불가 시 빈 optional을 반환한다. `Locked()`는 현재 접촉 고정 여부를 반환한다.
- 면 영역 겹침은 양쪽 사각형의 모서리 분리축(SAT)으로 판정한다. 단순 월드 AABB 근접에 의한 잘못된 보정을 피하고 회전/비균일 크기를 지원한다.
- TransformGizmo의 평면 이동 경로는 SurfaceSnap 적용 후 유효 부모 좌표로 환산하여 PreviewTransform을 호출한다. 회전과 꼭짓점 스케일에는 이 이동 보정을 적용하지 않으며, 모서리 스케일은 별도의 EdgeScaleSnap을 사용한다.

TransformGizmo는 드래그 시작 교차점 `planeStart`에 원시/보정 이동량을 더한 점들을 현재 VP 행렬로 투영합니다. 월드 거리 배율을 근사하는 대신 화면 좌표 차이를 직접 비교하여 원근 카메라와 확대/축소에서도 동일한 픽셀 범위를 유지합니다.


## 두 면 중심 축의 원형 회전

- `GizmoMath::ScreenOrbit::Reset(center, mouse)`는 화면상 선택 원 중심과 기준 포인터를 기록한다. `Apply(mouse)`는 원 중심 기준 atan2 각도 차를 누적하여 화면 반시계 방향을 양수로 반환한다. ±180도 경계를 unwrap하고 12픽셀 이내에서는 누적을 보류한다.
- `RotateWorldAxis(start, parent, center, axis, degrees)`는 월드 중심을 지나는 월드 축 주위로 시작 Transform을 회전한 뒤 부모 공간으로 환산한다. 두 면 중심의 위치, 비균일 크기 및 affine 보정을 보존한다.
- TransformGizmo는 선택 원과 반대편 원을 잇는 축 및 박스 중심을 드래그 시작 때 고정한다. 화면상 원형 이동 방향을 카메라에 대한 축 방향에 맞춰 변환하고 기존 RotationSnap을 적용한다. 예전 RotateFace 함수는 단일 면 피벗용 수학 API로 남아 있지만 우클릭 제스처에서는 사용하지 않는다.

TransformGizmo는 회전 시작 시 rotationEndA/B를 월드 좌표로 보관하고, 회전 중에만 현재 VP로 투영하여 축 선·양 끝 원·축 이름을 오버레이로 표시합니다. 입력 히트 테스트나 핸들 크기는 변경하지 않습니다.

`RotationSnap::Locked()`는 실제 보정 고정 상태를 반환합니다. TransformGizmo의 `rotationAngle`은 Apply가 반환한 적용 각도를 보관하며, 회전 오버레이는 이 값과 Locked 상태로 각도 텍스트·[SNAP] 표시·강조색을 결정합니다. 단순히 각도가 90의 배수인지로 보정 여부를 판단하지 않습니다.


## 편집 카메라

- `EditorCamera::Move(input, dt)`는 x=우측, y=월드 상향, z=시선 전방 입력을 받아 5유닛/초로 이동한다. 동시 방향 입력을 정규화하고 dt를 최대 0.1초로 제한한다.
- `Look(pixelDelta)`는 픽셀당 0.2도로 yaw/pitch를 변경하며 위치는 유지한다. pitch는 ±89도로 제한한다.
- `Pan(pixelDelta, viewportHeight)`는 카메라 Right/Up 평면에서 이동한다. 기준 깊이 10유닛과 FOV 45도를 사용해 화면 높이에 따라 이동량을 환산한다.
- `Get(width, height)`는 편집 상태로 PerspectiveCamera의 view/projection을 갱신해 반환한다.
- `SceneInspector::SceneCamera(width, height)`는 Scene 렌더링용 카메라를 반환한다. Sandbox의 render 콜백은 game 여부에 따라 기존 게임 카메라 또는 Scene 카메라를 선택한다.
- `NavigateCamera`는 View 모드의 Scene 포커스 및 캔버스 입력을 검사한다. 카메라 변경 시 해당 렌더 타깃만 다시 갱신하고, 객체 입력과 메뉴를 건너뛴다.


`EditorCamera::Zoom(distance)`는 시선 전방으로 카메라 위치만 이동합니다. 휠 스크롤 1단위는 1월드 유닛입니다. 휠 버튼 드래그는 이제 Zoom 대신 Orbit을 사용합니다. `CameraWheelGesture::Update(mouse, pressed, down, allowed, cancel)`는 orbitDelta/toggle/captured 결과를 반환합니다. 시작점에서 4픽셀 이상 이동하면 드래그로 확정하고, 드래그하지 않은 release에만 toggle을 반환합니다. SceneInspector는 두 모드의 공통 경로에서 처리하여 객체 입력과 분리합니다.


`EditorCamera::Orbit(pivot, pixelDelta)`는 월드 Y축 yaw 및 카메라 우측 축 pitch 회전을 카메라 위치와 시선에 함께 적용합니다. 대상까지의 거리와 대상의 뷰 공간 위치를 보존하며 pitch는 ±89도로 제한합니다. SceneInspector는 휠 시작 시 Scene 중앙 ray로 대상을 결정하고 월드 경계 중심을 orbitPivot에 저장합니다. 타겟이 없거나 드래그 중 삭제되면 월드 원점을 기준으로 회전합니다. `GizmoMath::Pick(scene, ray, selectGroups=false, renderedOnly=true)`는 콜라이더 전용 객체 및 빈 그룹 경계를 제외하며 그룹으로 선택을 승격하지 않습니다. 조준선은 버튼을 누르는 동안만 Scene 클립 영역에 표시합니다.

`GizmoMath::ScaleFromAnchor(target, start, mask, factor, anchor)`는 시작 로컬 행렬에 `T(anchor) * S(maskedFactor) * T(-anchor)`를 적용합니다. factor는 최소 0.02이며 반대편 꼭짓점/모서리 전체를 고정합니다. affine 보정을 유지해 shear에도 대응합니다. TransformGizmo는 시작 경계의 `low + high - grabbedPoint`를 고정점으로 저장하고 이 API를 사용합니다. 기존 ScaleSelected는 원점 기준 수학 API로 남아 있으며 View 핸들에서는 사용하지 않습니다.

`EdgeScaleFactors(world, handle, anchor, mask, initialRay, currentRay)`는 잡은 모서리에 수직인 평면에서 광선 교차점 차이를 시작 로컬 공간으로 변환하고, 두 축의 시작 span으로 나누어 독립 배율을 반환합니다. 교차 불가 시 빈 optional을 반환합니다. `ScaleFromAnchor`의 Vector3 factors 오버로드는 각 축을 독립적으로 최소 0.02로 제한하고 반대 모서리를 고정합니다. 기존 float 오버로드는 꼭짓점의 비례 조절용으로 유지합니다.


`GizmoMath::EdgeScaleSnap::Reset(scene, selected, handle, anchor, mask)`는 시작 geometry를 선택 객체의 로컬 공간으로 보관하고 변경 가능한 각 축의 면 접촉 후보를 구성합니다. `Apply(rawFactors, projectWorldToScreen)`은 각 축별 고정/해제/bypass 상태를 갱신해 보정 배율을 반환합니다. 보정 핸들과 원시 핸들의 화면 차이는 축마다 최대 10픽셀이며, 축 하나의 고정이 다른 축의 자유도를 막지 않습니다. 실제 보정 후 면의 평행성과 사각형 영역 겹침을 다시 확인합니다. TransformGizmo는 보정된 배율을 ScaleFromAnchor에 전달하므로 반대 모서리와 Undo 단위가 유지됩니다.

`SurfaceSnap::HoldPixels = 10.f`를 이동 및 EdgeScaleSnap이 공유합니다. `SurfaceSnap::ContactFaces(translation)`와 `EdgeScaleSnap::ContactFaces(factors)`는 현재 고정 중인 접촉 양쪽 면을 월드 좌표로 반환합니다. 비고정 상태는 빈 목록이며, TransformGizmo는 이 결과만 사용해 면과 활성 핸들을 민트색으로 강조합니다.


`Scene::PlaceSibling(source, reference, after)`는 같은 표시 부모를 가진 두 객체의 씬 내 순서를 변경합니다. after=false는 바로 앞, true는 바로 뒤입니다. `EditorGroups::InsertSibling(editor, source, reference, after)`는 대상의 표시 부모로 월드 변환을 유지하며 재배치한 후 순서를 변경하고 하나의 Undo 기록을 만듭니다. 같은 부모 안의 이동은 그룹 바인딩을 재설정하지 않습니다. 씬 직렬화가 객체 순서를 보존하므로 별도 파일 형식 변경 없이 순서를 저장합니다. HierarchyNode는 AcceptBeforeDelivery로 미리보기를 표시하고 실제 Delivery에서만 순회 후 변경을 예약합니다.


`Light::WorldPosition()`은 GetWorldMatrix의 위치를, `WorldDirection()`은 GetWorldRotation으로 합성한 회전만 사용합니다. 부모·그룹을 반영하며 방향에는 Scale을 적용하지 않습니다. SceneRenderer는 매 Render마다 이 값을 GPU에 전달합니다. Point는 월드 픽셀 위치에서 광원까지의 방향과 `max(1-distance/range,0)^2` 감쇠를 사용하고 Directional은 위치에 관계없는 방향을 사용합니다. range는 렌더링 시 최소 0.001로 제한합니다. SceneCodec 버전 3은 Light의 Type/Range를 추가로 저장하며 버전 1·2의 해당 필드는 기본값으로 채웁니다. LightRendering 테스트는 숨겨진 OpenGL 컨텍스트에서 실제 출력 픽셀로 이동·부모/그룹 이동·방향 회전·범위·활성화 변경을 검증합니다.

`GameObject::GetWorldRotation()`은 로컬 및 부모의 회전 quaternion을 합성합니다. 그룹 연결은 고정 바인딩 행렬에서 추출한 회전과 현재 그룹/공통 조상 회전을 조합하여 계산합니다. 현재 Scale/affine을 방향에 곱하지 않습니다. LightRendering은 비균일·음수·0 Scale에서 방향과 출력 픽셀이 유지되고 로컬/부모/그룹 Rotation에서 달라지는지를 검증합니다.


## 독립 Camera / Object 입력 소유권

SceneInspector의 moveView(Camera), moveObject(Object)는 독립 토글입니다. CameraPointerGesture는 4픽셀 이상 드래그에만 delta를 전달하고, 드래그하지 않은 release에만 click을 전달합니다. TransformGizmo의 handleHovered/Active/consumed가 있으면 카메라 제스처를 시작하지 않습니다. 이미 시작한 카메라 제스처 동안에는 핸들 입력을 차단합니다. Object가 꺼지면 Scene 핸들 Draw 및 객체 선택/메뉴 경로를 실행하지 않습니다. TransformGizmo 회귀 테스트에서 두 토글 동시 활성화 시 좌/우 핸들 드래그와 카메라 드래그의 상호 배제 및 두 토글 비활성 상태를 검증합니다.

## Material 및 Cook–Torrance 조명

`object.AddComponent<Material>()`은 같은 오브젝트의 MeshRenderer에 적용할 재질을 반환합니다. `material.metallicity`와 `material.roughness`는 0~1 범위의 float이며 기본값은 0과 0.5입니다. `material.albedoAsset`에 프로젝트 이미지 ID를 지정하면 Workspace::Update가 텍스처를 연결합니다. ID를 비우면 메시의 Color만 사용합니다.

`SceneRenderer::Render(scene, camera)`는 활성 Material을 가진 메시에서 광원마다 GGX 법선 분포, Schlick-GGX 기하 감쇠, Schlick 프레넬 항으로 Cook–Torrance 직접광을 계산합니다. 거칠기 0 부근의 수치 안정성을 위해 셰이더 입력에만 하한 0.045를 적용합니다. 씬과 클립보드는 Enabled, Metalicity, Roughness 및 Albedo asset을 보존합니다.

## 도형과 스크립트 템플릿

MeshRenderer::shape는 BasicShape 열거형의 정수값을 보관합니다. 순서대로 Cube=0, Circle=1, Rectangle=2, Sphere=3, Cylinder=4, Cone=5입니다. 기존 씬의 누락된 Shape는 Cube로 초기화합니다. 렌더러는 도형별 위치, 법선, UV 및 삼각형 인덱스로 메시를 생성합니다.

ObjectScript와 BlankScript는 MonoBehaviour의 수명주기 콜백을 상속하는 기본 템플릿입니다. SingletonManager::Instance(Scene&)는 해당 씬에서 처음 활성화된 매니저를 반환하고, 없으면 nullptr을 반환합니다. IsInstance()는 자신이 해당 인스턴스인지 확인합니다. 수동 복제한 매니저 중 첫 활성 매니저가 조회 대상입니다. 씬 간 전역 포인터를 공유하지 않습니다. 각 스크립트의 Enabled와 도형 종류는 씬 저장, 복제 및 Undo에 포함됩니다.

RotationSnap::Apply는 해제 직후의 미세 조절 우회를 해당 90도 구간에 한정합니다. 가장 가까운 90도 중심이 바뀌면 우회를 해제해 전진과 역진 모두에서 스냅을 재활성화합니다.

## 큐브맵 전개도 판별

DetectCubemapLayout(width, height)는 정확한 정사각형 셀 비율을 검사해 None, VerticalCross, HorizontalCross, HorizontalStrip 또는 VerticalStrip을 반환합니다. CubemapLayoutName(layout)은 Inspector 표시 이름을 반환합니다.

SceneRenderer는 Cube 메시의 활성 Material.albedoTexture 크기를 검사하고 로컬 면 방향에 따라 전개도의 셀을 선택합니다. 세로 십자의 윗면, 아랫면, 뒷면은 펼친 방향에 맞춰 회전합니다. 셀의 반 픽셀 안쪽으로 UV를 제한해 이웃 셀의 색상 유입을 줄입니다. 오브젝트의 회전과 부모 변환은 면 선택을 바꾸지 않습니다. Sprite와 Cube 이외의 도형은 기존 UV를 사용합니다.


## DynamicReflection 및 ReflectionProbe

ReflectionProbe는 Component이며 resolution, storageQuality, maxObjectSize, fixEdgeSeams를 씬과 클립보드에 보존합니다. 코드에서 AddComponent<ReflectionProbe>()로 추가합니다. storageQuality는 0=RGB8, 1=RGB16F입니다.

`DynamicReflection::Capture(probe, precise, draw)`는 여섯 방향의 90도 카메라로 `draw`를 호출하고 GPU 큐브맵의 mipmap을 생성합니다. `precise`는 해당 촬영 시점에 정밀 반사용 기하도 준비했는지 나타냅니다. 해상도 또는 포맷 변경 시 저장 공간을 재할당합니다. 실패 시 프레임버퍼, 뷰포트, 깊이와 시저 상태를 복원하고 예외를 전달합니다. `Texture()`는 GPU 큐브맵 ID, `MipLevels()`는 전체 mip 단계 수를 반환합니다.

`MeshRenderer::reflectionMode`의 값은 `ReflectionMode::Off`(0), `Direct`(1), `Precise`(2)이며 기본값은 `Precise`입니다. `SceneRenderer::Render`는 활성 재질을 가진 가시 수신 메시마다 자체 프로브를 우선 선택하고, 없으면 가장 가까운 활성 프로브를 사용합니다. `Direct`는 표면 반사 방향으로 큐브맵을 샘플링하고, `Precise`는 반사상의 위치를 보정합니다. 두 모드 모두 거칠기에 따른 mip 단계와 프레넬을 적용합니다. `Off` 메시도 다른 프로브의 촬영에는 참여할 수 있습니다. 내부 촬영에서는 `capturing=true`로 재귀 촬영을 방지하고 `excluded` ID의 오브젝트를 제외합니다.

`ReflectionProbe::maxObjectSize`의 기본값은 100px이며 저장 필드 이름은 `Max object size`입니다. `ReflectionPixelSize(world, viewProjection, viewport, low, high)`는 near/far 평면과 뷰포트로 자른 메시 경계의 너비와 높이 중 큰 픽셀 수를 반환합니다. 기준 미만인 `Precise` 기본 도형은 카메라 쪽을 향한 대표 면으로 픽셀별 반사 위치를 계산합니다. 커스텀 메시는 표면 법선을 사용합니다.

프로브를 사용하는 가시 수신 메시가 하나라도 기준 이상이면 큐브맵을 갱신합니다. 작은 메시만 보이면 마지막 촬영을 재사용할 수 있습니다. `DynamicReflection::SettingsChanged(probe, precise)`는 첫 촬영, 해상도·포맷 변경, 정밀 기하가 없는 촬영본에 정밀 수신 메시가 추가된 경우를 검출합니다. 조명·그림자 버전이 바뀔 때도 촬영을 갱신합니다. 씬 인스턴스와 오브젝트 ID별로 큐브맵을 관리하고 삭제되거나 비활성화된 프로브의 GPU 자원을 해제합니다. Fix up edge seams는 해당 렌더링 동안 `GL_TEXTURE_CUBE_MAP_SEAMLESS`를 적용하고 이전 전역 상태로 복원합니다.


## 반사상 위치와 크기 보정

`DynamicReflection::SetGeometry(groups, payload, owners)`는 정밀 반사를 받는 메시가 보일 때 큐브맵과 같은 시점의 기하를 `RGBA32F` 텍스처 버퍼에 올립니다. 메시 또는 스프라이트당 헤더 3텍셀에 소유자 인덱스, 도형 종류, 데이터 길이와 월드 경계 상자를 저장합니다. 헤더 배열 뒤에는 기본 도형의 역월드 행렬(4텍셀) 또는 커스텀 메시의 월드 정점(삼각형당 3텍셀)이 이어집니다. 삼각형이 64개 이상인 커스텀 메시에는 BVH 노드 데이터도 포함합니다. 업로드 전에 전체 텍셀 수를 `GL_MAX_TEXTURE_BUFFER_SIZE`와 비교합니다. `GeometryTexture()`는 버퍼 텍스처 ID, `GeometryGroupCount()`는 그룹 수, `ReceiverIndex(id)`는 해당 오브젝트의 첫 그룹 인덱스 또는 −1, `CapturePosition()`은 마지막 촬영 위치를 반환합니다.

`ReflectionIntersectionGLSL`의 `correctedReflectionDirection(origin, direction)`은 수신 오브젝트와 프로브 자체를 제외하고 월드 반사선이 통과하는 경계 상자를 찾습니다. 해당 상자 안의 커스텀 삼각형은 월드 좌표에서 검사하고, 기본 도형은 반사선을 로컬 좌표로 옮겨 교차점을 계산합니다. 이때 로컬 반사 방향의 길이를 유지하므로 비균일 스케일과 전단 변환이 걸린 도형의 교차 거리도 비교할 수 있습니다. 가장 가까운 교차점이 있으면 마지막 프로브 촬영 위치에서 그 지점으로 향하는 방향을, 없으면 0 벡터를 반환합니다.

큰 커스텀 메시는 `MeshRenderer::Intersection()`에 캐시된 삼각형 BVH를 재사용합니다. CPU는 가장 긴 축의 중앙값에서 삼각형을 분할해 리프 하나에 최대 8개를 배치합니다. GPU용 노드당 2텍셀에 월드 경계 상자, 하위 트리를 건너뛸 거리, 리프의 삼각형 수를 담습니다. 셰이더는 별도 스택 없이 반사선과 만나는 노드만 내려가고, 리프에서 원본 삼각형과 교차하는 가장 가까운 점을 구합니다.

도형별 교차는 단위 박스, XY 원/사각형, 구, Y축 원기둥과 원뿔을 사용합니다. 곡면은 해석적 도형으로 계산하므로 렌더링 메시의 세분화 경계와 소폭 차이가 날 수 있습니다. 촬영 시점의 가림과 투명 이미지의 alpha cutout은 교차 기하에 포함되지 않아 해당 부분은 큐브맵 근사로 남습니다.
