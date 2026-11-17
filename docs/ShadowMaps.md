# 방향광·포인트 라이트 섀도맵

기본 방향광과 `Light` 컴포넌트의 그림자가 기본 바닥, 일반 메시, 큐브맵 반사에 반영된다. 기존 최대 8개 광원 순서를 그대로 사용하며 기본 방향광도 슬롯 하나를 차지한다.

## 사용

- 기본 바닥은 Scene 설정의 Ground를 켠다. 기본 방향광 또는 활성 Light가 있으면 가림 물체 아래에 그림자가 생긴다.
- Light의 Type은 Point/Directional 중 선택한다. Point의 Range는 조명과 그림자의 영향 범위에 함께 적용된다.
- Light, MeshRenderer, SpriteRenderer의 `Cast shadows`는 기본 true이며 인스펙터에서 개별적으로 끌 수 있다. 씬·프리팹·복사·실행 취소에 보존된다. 이전 파일에 필드가 없으면 true를 사용한다.
- SpriteRenderer는 기존처럼 unlit로 표시되지만 알파 마스크를 따라 그림자를 드리운다.
- 기본 바닥은 기존 회색과 주변광을 유지하는 shadow catcher다. 그림자를 받으며 자체적으로 다른 물체에 그림자를 드리우는 무한 평면 caster는 아니다.

```cpp
renderer.SetShadowsEnabled(true);       // 기본값
renderer.SetShadowResolution(512);      // 기본값, 64~2048로 제한
const auto& stats = renderer.GetShadowStats();
// stats.updates: 이번 외부 Render 호출에서 갱신한 광원 수
// stats.faces: 그린 깊이 패스 수 (방향광 1, 포인트 6)
```

성능 비교용으로 `SetShadowCullingEnabled(bool)`와 `SetShadowCachingEnabled(bool)`를 독립적으로 전환할 수 있다. 둘 다 기본 true다. 컬링을 꺼도 장면 스냅샷·월드/경계 캐시·BVH 유지 비용은 남으며, 깊이 뷰의 후보 선택만 전체 순회로 바뀐다. 캐싱을 끄면 모든 활성 그림자 광원의 깊이맵을 매 프레임 갱신한다.

`SetShadowSelectiveInvalidationEnabled(bool)`도 기본 true다. false로 설정하면 이전의 장면 전체 caster 변경에 따른 전역 무효화를 사용한다. 비교 경로도 월드/경계 캐시와 깊이 뷰 컬링을 계속 사용한다. 모드 전환 자체는 모든 활성 그림자 광원을 한 번 무효화한다.

`GetShadowDrawStats()`는 외부 Render 한 번의 `candidates`(갱신한 깊이 면 수 × 적격 caster 컴포넌트 수), `submitted`(컬링 후 제출한 컴포넌트 수), `drawCalls`(실제 깊이 드로 호출 수), `instances`(깊이 패스 큐브 인스턴스 수)를 제공한다. 메시와 스프라이트를 함께 가진 물체는 두 컴포넌트로 센다. 캐시 적중 시 모두 0이며, BVH 내부 노드 검사 횟수는 포함하지 않는다. 재현 가능한 5개 모드 실측은 [섀도맵 성능 비교](benchmarks/shadow-performance.md)에 기록한다.

## 처리 순서와 재사용

1. 활성 광원을 수집한다.
2. 기존 SceneSpatialCache에서 월드 행렬·경계와 장면 BVH를 준비한다.
3. 바뀐 광원/가림 데이터의 깊이맵만 갱신한다. 각 깊이 뷰는 BVH 시야체 질의로 가림 후보를 선택하고 스냅샷의 월드 행렬을 사용한다. 텍스처 없는 큐브는 인스턴싱한다.
4. 반사 프로브의 컬러 큐브맵을 캡처한다. 캡처 셰이더는 이미 생성한 섀도맵을 읽으며 깊이맵을 재귀적으로 생성하지 않는다.
5. 메인 뷰의 메시와 기본 바닥도 같은 섀도맵을 사용한다.

방향광은 장면과 바닥 투영 범위를 덮는 직교 깊이맵을 쓴다. 포인트 라이트는 광원 중심의 90도 뷰 6개를 쓴다. `CubeCapture.h`의 방향/up 규칙을 반사 큐브맵과 공유한다.

OpenGL 3.3의 sampler 배열 인덱싱/텍스처 유닛 제한을 피하기 위해 깊이 텍스처 배열 한 개에 방향광 1레이어, 포인트 광원 6레이어를 연속 배치한다. 포인트 그림자는 큐브맵과 같은 6방향 표현이지만 GPU 리소스는 `GL_TEXTURE_2D_ARRAY`다. 텍스처 유닛 3을 사용한다.

색상 패스와 깊이 패스는 `MaterialUV.h`의 UV 계산을 공유한다. 커스텀 메시의 삼각형 재질, 큐브 아틀라스, 스프라이트 UV와 `alpha < 0.1` 제거를 유지한다. 기본 바닥은 월드 교점에서 같은 shadowVisibility를 평가한다. 반사 교차검사에 y=0 바닥 교차도 추가해 반사 속 바닥과 그림자가 사라지지 않도록 했다.

## 캐시 정책

- 정적 장면에서는 깊이 패스가 0회이며 FBO 상태 변경도 생략한다.
- 물체별 월드 행렬·경계·localScale·형상 revision·활성 상태·cast 설정·알파·마스크 텍스처·스프라이트 UV를 비교해 변경 caster를 찾는다. 해시 대신 실제 데이터와 약한 텍스처 소유권을 비교하므로 해시 충돌이나 텍스처 수명 연장이 없다.
- `ShadowInvalidation.h`는 변경 caster의 **이전 경계와 현재 경계를 별도 AABB로** 모아 기존 `SpatialBVH` 구현으로 작은 BVH를 만든다. 삭제·비활성화·cast 해제는 이전 경계, 추가·재활성화는 현재 경계가 남는다. 순간이동 경로 전체를 합친 큰 AABB를 사용하지 않는다. 정적 프레임은 구축을 생략한다.
- 포인트 광원은 깊이 6면이 덮는 `position ± range` 큐브와 변경 경계 BVH의 교차를 검사하고 첫 교차에서 탐색을 끝낸다. 구 범위로 좁히면 실제 깊이 패스의 모서리나 PCF 의존성을 놓칠 수 있으므로 큐브와 좌표 크기에 비례하는 작은 허용 오차를 사용한다. 교차한 광원의 revision만 증가시킨다. 비정상/비유한 경계는 전체 갱신으로 처리한다.
- 방향광은 caster 변경 시 보수적으로 갱신한다. caster가 아닌 렌더러가 장면 범위를 바꾸어 방향광 투영 행렬이 달라진 경우도 기존 행렬 비교로 갱신한다.
- 광원 위치/방향/범위, 깊이맵 해상도, 장면 교체, 광원 슬롯/레이어 재배치도 필요한 맵을 갱신한다.
- 광원의 색상·세기만 바뀌면 깊이는 재사용한다. 불투명 메시의 RGB 색이나 거칠기는 깊이에 영향을 주지 않는다.
- 주 카메라 이동만으로는 깊이맵이 갱신되지 않는다. 방향광은 카메라 추적 방식이 아니라 장면 범위에 맞춘다.
- 그림자 또는 광원 상태가 바뀌면 기존에 작은 물체용으로 동결된 반사 프로브도 다시 캡처한다. 변경 caster가 모든 광원 밖에 있어 깊이 갱신이 0회여도 반사 버전은 증가시켜 과거 반사 기하가 남지 않게 한다.
- 직접 커스텀 메시 데이터를 수정할 때는 기존 GPU/메시 BVH 캐시와 마찬가지로 `custom.Touch()`를 호출해야 한다. Texture2D는 현재 로드 후 불변 API이며, 변경된 마스크는 새 텍스처로 교체한다.

`GetShadowInvalidationStats()`의 `changedCasters`는 변경/추가/제거된 물체 기록 수, `changedBounds`는 이전·현재의 유효 변경 경계 수, `bvhBuilds`는 변경 경계 BVH 구축 수다. `affectedLights`는 caster 때문에 무효화한 광원 수이며 광원 자체 이동·해상도 변경에 따른 갱신은 포함하지 않는다. 전역 비교 모드는 개별 변경 검사와 BVH를 생략해 처음 두 카운터가 0(미계측)이다. 동적 성능과 독립 렌더러의 연속 프레임 검증은 [선택 갱신 실측](benchmarks/shadow-invalidation.md)에 기록한다.

## 품질과 범위

깊이 비교에 normal/depth bias와 3×3 PCF를 적용한다. 방향광은 단일 맵이며 CSM이 아니므로 아주 넓은 장면에서는 그림자 선명도가 낮아질 수 있다. 수평에 가까운 방향광의 바닥 투영 거리는 무한대로 늘리지 않고 제한한다. 포인트 PCF는 각 면의 경계에서 해당 면 안으로 제한하므로 면을 가로지르는 필터의 완전한 연속성을 보장하지 않는다.

깊이맵은 이진 알파 가림을 지원한다. 색이 있는 투명 그림자나 반투명 광투과, 면광원/물리적 연성 그림자는 구현하지 않는다. 기존 GPU 반사 교차검사의 삼각형 선형 탐색은 이번 변경과 별개다.

## 검증

```powershell
cmake --build Build/logging --config Release --parallel 4
ctest --test-dir Build/logging -C Release --output-on-failure
New-Item -ItemType Directory -Path Build/benchmarks -Force | Out-Null
./Build/logging/bin/Release/RiceShadowRenderTests.exe Build/benchmarks/shadow-preview.bmp
```

`ShadowRenderTests`는 실제 숨김 OpenGL 컨텍스트에서 그림자 on/off 이미지를 비교한다. 방향광/포인트/기본 바닥/메시/6개 포인트 면/알파 마스크/반사된 메시와 바닥을 검사하며, 정적 캐시 재사용·광원 이동·가림 물체 변경·광원 색/세기 변경·레이어 재배치·해상도 변경·삭제도 검증한다. Cast shadows의 씬 및 이전 프리팹 호환성도 검사한다.

2026-11-16 최종 Release 전체 빌드와 46개 CTest가 모두 통과했다. Windows SDK/파이프 접근이 필요한 테스트를 포함해 일반 권한 환경에서 실행했다. 방향광과 포인트 라이트 바닥 샘플의 평균 밝기는 그림자 해제 시 184, 활성 시 각각 26과 약 33이었으며, 반사된 기본 바닥 샘플은 222에서 70으로 감소했다. 이는 회귀 장면의 검증값이며 일반적인 성능/밝기 보장은 아니다.
