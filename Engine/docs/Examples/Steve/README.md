# Steve / AABB / Point-light presentation

한 카메라 안에서 Steve, AABB, 포인트 광원의 그림자와 캐시 재사용을 설명하는 발표용 장면이다. Play를 누르면 광원에서 공이 차례로 나오고, 금속 큐브가 이동·회전할 때 경계와 빛의 경로, 실제 섀도맵이 갱신된다.

## 완성된 발표 영상

[Steve-Shadow-Demo-1080p60.mp4](Recordings/Steve-Shadow-Demo-1080p60.mp4) — **13초 / 1920×1080 / 60fps / H.264 / 무음**.

공이 없는 첫 프레임부터 방출, 큐브 이동, 정지 중 캐시 재사용, 복귀까지 끊김 없이 담았다. 현재 저장된 장면을 실제 엔진 렌더러로 1/60초씩 진행해 직접 캡처했으며, 녹화 속도는 실행 PC의 순간 프레임 속도와 무관하다. 화면의 갱신 수치는 실제 렌더러 통계이며, 한글 진행 자막과 진행 막대를 덧붙였다. 기존 장면 파일은 수정하지 않았다.

780개 프레임 전체를 다시 디코딩해 1080p·60fps·13초와 연속 타임스탬프를 확인했다. 이동 중 섀도맵 6면/BVH 조회 12회, 정지 중 0면/조회 0회도 검사했다. 측정 CSV와 디코딩 확인 이미지는 발표 폴더에서 정리했다. 다시 녹화하면 검증 파일은 `Engine/Cache/recording-verification/`에 생성된다.

새 이름으로 다시 녹화하려면 다음을 실행한다.

```powershell
cmake --build Engine/Cache/rice-release --config Release --target RiceShadowRecording --parallel 4
./Engine/Cache/rice-release/bin/Release/RiceShadowRecording.exe Projects/Steve Engine/docs/Examples/Steve/Recordings/new-recording.mp4
```

## 열기와 녹화

1. 최상단 `Rice.exe`를 실행하고 **File → Open Project**에서 `Projects/Steve/Steve.rice`를 연다.
2. 에디터의 **Game** 뷰를 선택하면 `06 | RECORDING CAMERA` 구도가 보인다.
3. **Play**를 누르면 처음에는 공이 보이지 않고, 광원에서 순차적으로 방출된다. Pause는 정지 설명, Stop은 초기 편집 상태 복귀에 사용한다.
4. 에디터에서 **Build standalone game**으로 만든 게임 EXE를 실행하면 이 장면이 재생된다. 종료는 Alt+F4다. 녹화 도구 사용법은 위 항목을 참고한다.

[Steve.rice](../../../../Projects/Steve/Steve.rice)에 씬과 프로젝트 설정이 저장된다. 완성된 게임은 빌드된 EXE 하나를 다른 PC로 복사하여 실행한다.

## 자동 진행과 직접 조작

| Play 이후 시간 | 큐브와 화면의 동작 |
| --- | --- |
| 0–3초 | 큐브는 Steve의 손 위에 머물고, 공들이 차례로 방출된다. |
| 3–5초 | 큐브가 이동·회전한다. 현재 AABB와 경로, 그림자가 갱신된다. |
| 5–8초 | 이동한 자세로 멈춘다. 공은 계속 이동하고, 섀도맵은 캐시를 재사용한다. |
| 8–10초 | 큐브가 원래 자세로 돌아온다. 경로와 그림자가 다시 갱신된다. |
| 10–13초 | 원래 자세로 멈추고 캐시를 재사용한다. |

최초 3초 준비 구간 이후에는 이동 2초, 정지 3초, 복귀 2초, 정지 3초의 **10초 주기**가 반복된다. 직접 움직이며 설명하려면 `00 | DEMO CONTROLLER`의 `Shadow Presentation` 컴포넌트에서 **Auto motion**을 끈 뒤 Play 중 금속 큐브의 위치·회전을 조작한다. 이 설정은 코드의 `autoMotion = false`에 해당한다. Play 중 편집은 Stop 시 초기 상태로 돌아간다.

## 구성

- Steve: 기존 `Sandbox/Assets/steve.png`를 수정 없이 복사하고 여섯 몸체 부위에 실제 UV를 매핑했다. 오른쪽 팔을 들어 큐브를 받치는 자세다. 48정점/72삼각형의 custom mesh다.
- 청록색 AABB: 실제 Steve 메시의 월드 경계를 12개 얇은 큐브 모서리로 표시한다. 최소 `(-0.2, 0, -0.4)`, 최대 `(2.039755, 3.2, 1.103135)`. 금속 큐브는 별도 물체이므로 이 경계에 포함하지 않는다. Steve의 초기 자세를 표시하는 고정 테두리다.
- 초록색 AABB: 움직이는 금속 큐브의 현재 월드 경계를 매번 반영한다. 큐브가 회전해도 경계 상자는 월드 축과 나란하다.
- 주황색 AABB: 큐브가 움직이기 전 마지막으로 멈춰 있던 자세의 경계다. 변화 범위를 알아보기 쉽게 남기는 **발표용 표시**이며, 엔진의 선택적 섀도 갱신이 검사하는 직전 프레임 경계와는 다르다.
- 금속 큐브: 회전 `(18, -24, 24)`도, 금속도 0.94, 거칠기 0.13, 256² 반사 프로브. 반사에도 같은 섀도맵을 사용한다.
- 포인트 광원: 실제 Light 컴포넌트는 1개다. 구체와 작은 고리는 광원의 위치를 표시한다.
- 공: 12개 경로마다 7개씩, 총 **84개의 공을 미리 만들어 재사용**한다. 각 경로에서 0.48초 간격으로 방출하며 각 공은 3.36초마다 재사용한다. 현재 메시의 첫 교점 또는 바닥 앞에서 사라지고, 화면에 동시에 보이는 수는 경로 길이에 따라 달라진다. 공은 **빛의 진행 방향을 설명하는 시각 표식**이다. 물리 시뮬레이션이나 실제 광자 추적 결과를 나타내지 않으며, 별도의 빛이나 그림자를 만들지 않는다.
- 기본 바닥이 Steve와 큐브의 그림자를 받는다. 카메라는 고정되어 있어 녹화 중 조작할 필요가 없다.

Hierarchy의 `02 | Steve AABB` 또는 `05 | Light ray markers`의 Active를 끄면 설명용 표시를 각각 숨길 수 있다.

## 화면에서 확인할 재계산과 캐시

공의 경로는 Steve와 큐브의 월드 AABB를 BVH로 좁힌 뒤, 재사용한 메시 BVH에서 실제 표면 교점을 구한다. 큐브·Steve·광원이나 경로 설정이 바뀌면 경로를 다시 검사한다. 변환만 바뀔 때는 월드 행렬과 경계를 갱신하고 메시 BVH는 재사용한다. 공만 움직이는 동안에는 계산된 경로를 그대로 사용한다. 청록색 Steve 테두리는 고정 표시지만, 경로 검사는 Steve의 현재 변환을 반영한다.

상태 표시는 실제 렌더러의 통계를 읽는다. 이 장면의 포인트 광원은 섀도맵을 갱신할 때 큐브맵 **6면**을 렌더링하고, 재사용할 때는 **0면**을 렌더링한다. 큐브가 멈춘 뒤에도 공이 계속 움직이면서 `DEPTH CACHE REUSED`가 표시되는 부분으로 캐시의 효과를 설명할 수 있다. 경로의 BVH 조회 수와 보이는 공의 수도 함께 표시한다.

에디터의 Scene과 Game 뷰는 같은 섀도 캐시를 공유한다. 먼저 그린 뷰에서 6면을 갱신하고 다음 뷰에서 재사용하면, 화면에 `NOW: 0`과 `LAST REBUILD: 6`이 함께 표시될 수 있다. `NOW`는 가장 최근 렌더의 실제 갱신 수이고, `LAST REBUILD`는 마지막으로 갱신한 면 수와 경과 시간이다.

## 재생성 및 확인

```powershell
python Engine/scripts/build-steve-asset.py
cmake --build Engine/Cache/rice-release --config Release --target RiceShadowPresentation RiceSandbox RicePlayer RicePresentationSceneTests RiceShadowRenderTests RiceShadowInvalidationTests --parallel 4
./Engine/Cache/rice-release/bin/Release/RiceShadowPresentation.exe Projects/Steve
ctest --test-dir Engine/Cache/rice-release -C Release -R "PresentationScene|ShadowRender|ShadowInvalidation" --output-on-failure
```

재생성은 이 프로젝트의 `Steve.rice`에 현재 장면을 다시 저장한다. 직접 편집한 장면을 유지하려면 먼저 다른 이름으로 저장한다. 생성기는 SceneCodec 저장/불러오기와 장면 진행을 검사하고, 실제 엔진 렌더로 `Engine/Cache/presentation-preview/`에 다음 비교 이미지를 만든다.

- `preview.bmp`: 공이 아직 방출되지 않은 시작 상태.
- `preview-emission.bmp`: 큐브가 멈춰 있고 공이 방출되는 상태.
- `preview-motion.bmp`: 큐브가 움직이며 경계·경로·그림자가 갱신되는 상태.
- `preview-hold.bmp`: 이동 후 멈춘 자세.
- `preview-cached.bmp`: 0.5초 동안 공만 더 이동한 뒤 섀도 캐시를 재사용하는 상태.

`PresentationScene` 테스트는 순차 방출, 큐브·광원 이동에 따른 경로 교점 변화, 정지 시 조회 재사용, 자동 이동·정지·복귀와 수동 조작을 확인한다. 실제 렌더러를 통해 섀도 갱신이 `6 → 0 → 6 → 0`면으로 바뀌는지도 검사한다. `ShadowRender`와 `ShadowInvalidation`은 기존 그림자 렌더링과 선택 갱신 경로의 회귀를 검사한다.

검증 완료: 에디터·플레이어·장면 생성기 빌드, SceneCodec 저장/불러오기, 실제 엔진의 방출·이동·정지 프레임 렌더, 위 3개 테스트 통과, 숨김 플레이어 120프레임 실행(exit 0). 에디터 Game 뷰의 Play 화면에서도 공의 방출과 실제 6면 갱신/0면 재사용 표시를 확인했다. 최종 장면을 다시 불러와 오른쪽 상태 표시가 광원을 가리지 않는 것도 확인했다.



