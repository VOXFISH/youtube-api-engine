# Rice Engine

최상단 실행 파일을 직접 열어 사용합니다.

- **[RiceEditor.exe](RiceEditor.exe)**: 엔진 에디터. **File → Open Project**에서 아래 프로젝트를 선택합니다.
- **[RicePlayer.exe](RicePlayer.exe)**: 빌드된 플레이어. 더블클릭하면 Steve 발표 장면을 바로 재생합니다.
- **[RiceController.exe](RiceController.exe)**: 프로젝트별 플레이어 실행·일시정지·종료를 관리합니다.

## 테스트씬

두 테스트씬과 영상은 최상단 **[Presentation](Presentation/README.md)** 폴더에 있습니다.

| 장면 | 에디터에서 열 파일 |
|---|---|
| Grass1000 | `Presentation/Grass1000/project.rice` |
| Steve / AABB / 섀도맵 | `Presentation/Steve/project.rice` |

프로젝트를 연 뒤 **Game → Play**로 실행합니다. Steve 장면은 공 방출, 큐브 이동, 섀도맵 갱신, 캐시 재사용과 복귀를 자동으로 보여 줍니다. [완성 영상](Presentation/Steve/Recordings/Steve-Shadow-Demo-1080p60.mp4)은 13초·1080p·60fps입니다.

다른 PC로 옮길 때는 최상단의 실행 파일·DLL·`RicePlayer.control`과 `Presentation`, `ThirdParty` 폴더를 함께 복사합니다. 실행에는 Windows x64와 OpenGL 3.3 이상이 필요합니다.

## 개발과 빌드

`Engine`은 엔진 소스, `Sandbox`는 앱·스크립트·원본 에셋, `RiceController`는 컨트롤러 소스입니다. 회귀 테스트는 `Tests`, 수동 성능 비교 도구는 `Tests/Benchmarks`, 개발 자료는 `docs`에 있습니다.

Visual Studio C++ 도구, Windows SDK, CMake, vcpkg, FMOD SDK를 준비한 뒤 실행합니다.

```powershell
$env:VCPKG_ROOT = 'C:/path/to/vcpkg'
.\Build.ps1 -FmodRoot 'C:/Program Files (x86)/FMOD SoundSystem/FMOD Studio API Windows'
```

빌드·테스트가 성공하면 실행 파일과 DLL을 **저장소 최상단**에 설치합니다. 저장된 두 장면은 유지합니다. 기존 의존성은 `-InstalledDependencies <vcpkg_installed 경로>`로 재사용할 수 있습니다.

생성 파일은 `Build` 아래에 저장합니다. 벤치마크 결과는 `Build/benchmarks`, 장면 미리보기는 `Build/presentation-preview`, 녹화 검증 파일은 `Build/recording-verification`입니다. 이전 실행본·프로젝트·CMD 진입점은 `Build/legacy-backup`에 보관했습니다.

`./scripts/Verify-Demo.ps1`은 개발 SDK 경로 없이 에디터와 두 프로젝트의 실행을 검사합니다. `./scripts/Verify-MultiInstance.ps1`은 독립 플레이어의 다중 실행을 검사합니다.

[사용 설명서](docs/EngineManual.md) · [엔진 API](docs/EngineFunctions.md) · [섀도맵 구현](docs/ShadowMaps.md) · [컨트롤러 안내](RiceController/README.md)
