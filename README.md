# Rice Engine

Windows x64용 C++20 / OpenGL 에디터입니다. 실행에 필요한 DLL은 실행 파일 옆에 포함되어 있습니다.

## 시연

1. 최상단의 **RiceEditor.exe**를 실행합니다. 프로젝트 없이 빈 편집 창으로 시작합니다.
2. **File → Open Project**에서 **TestBuild/Project/project.rice**를 선택합니다.
3. Grass1000 씬이 열립니다. Hierarchy에서 씬을 선택하고, Play / Stop으로 실행과 편집을 전환합니다.
4. **Window → Benchmark**에서 진단을 예약합니다. 진단을 실행할 때만 함수 로그가 생성됩니다.

원래 실행본은 **TestBuild/RiceSandbox.exe**입니다. 실행하면 함께 보존한 Grass1000 프로젝트를 자동으로 엽니다.

**RicePlayer.exe**는 독립 게임 실행용입니다. 편집기의 Export를 사용하거나 `RicePlayer.exe --project TestBuild/Project`로 실행합니다.

## 폴더

| 경로 | 내용 |
|---|---|
| `RiceEditor.exe`, `RicePlayer.exe`, `*.dll` | 최신 실행 파일과 런타임 |
| `Build.cmd`, `Build.ps1`, `CMakeLists.txt`, `CMakePresets.json` | 빌드 진입점과 설정 |
| `TestBuild/` | 정리 전 에디터, 런타임, 샘플 프로젝트 보존본 |
| `Engine/` | 엔진과 에디터 소스 |
| `Sandbox/` | 앱 진입점, 스크립트, 샘플 에셋과 Grass1000 프로젝트 |
| `Tests/` | 엔진·에디터 회귀 테스트 |
| `docs/` | 사용 설명서, 함수 문서, 개발 자료 |

`Build/`는 빌드할 때 다시 생성되는 작업 폴더입니다. 로그, 임시 파일과 빌드 중간 산출물은 커밋하지 않습니다. `TestBuild`는 다시 빌드해도 덮어쓰지 않습니다.

## 다시 빌드

Visual Studio 2022 또는 2026의 C++ 데스크톱 개발 도구, Windows SDK, 해당 생성기를 지원하는 CMake, vcpkg, FMOD Studio API Windows Core SDK가 필요합니다.

```powershell
$env:VCPKG_ROOT = 'C:/path/to/vcpkg'
.\Build.ps1 -FmodRoot 'C:/Program Files (x86)/FMOD SoundSystem/FMOD Studio API Windows'
```

환경이 준비되어 있으면 **Build.cmd**로도 실행할 수 있습니다. 구성 → Release 빌드 → 전체 테스트 → 최상단 실행 파일 갱신 순서로 진행합니다. 최초 의존성 설치에는 네트워크가 필요합니다. 실행 중인 에디터를 닫고 빌드하세요.

## 배포본 실행 검사

실행 대상은 **Windows x64 + OpenGL 3.3 이상**입니다. 다른 운영체제용 실행 파일은 포함하지 않습니다. 실행만 할 때에는 Visual Studio, vcpkg, FMOD SDK가 필요하지 않습니다.

`powershell -File scripts/Verify-Demo.ps1`은 실행 파일·DLL·프로젝트만 별도 폴더에 복사하고, SDK 환경변수와 개발 도구 PATH를 제거한 상태에서 5가지 시작/불러오기 검사를 실행합니다. 결과는 `Build/portability/`에 기록됩니다. GitHub의 [Demo package smoke](https://github.com/VOXFISH/youtube-api-engine/actions/workflows/demo-smoke.yml)는 별도 Windows Server 2022에서 같은 검사를 실행합니다. CI의 Mesa 소프트웨어 OpenGL은 테스트 복사본에만 적용하며 배포본의 그래픽 드라이버를 바꾸지 않습니다.

자세한 편집·프로파일링 방법은 [사용 설명서](docs/EngineManual.md), 엔진 API는 [함수 문서](docs/EngineFunctions.md)를 참고하세요.
