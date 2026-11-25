# Rice

설치 폴더에서 작업에 맞는 프로그램을 실행합니다.

| 프로그램 | 용도 |
|---|---|
| [Rice.exe](../Rice.exe) | 프로젝트를 만들고 씬과 RiceMonkey 스크립트를 편집합니다. |
| [RicePlayer.exe](../RicePlayer.exe) | 게임 빌드를 선택해 로컬에서 실행합니다. 게임 EXE를 직접 실행해도 됩니다. |
| [RiceCon.exe](../RiceCon.exe) | 게임 빌드를 등록하고 YouTube 송출을 운영합니다. |

## 프로젝트 만들기

Rice의 **File → New Project**에서 이름과 저장 위치를 선택합니다. 기본 위치는 `Projects`입니다. 프로젝트를 다시 열 때는 **File → Open Project**에서 프로젝트 최상단의 `<프로젝트명>.rice`를 선택합니다. 이 파일 하나에 씬과 입력 매핑이 저장됩니다. 프로젝트를 연 뒤 **Import...**를 누르면 FBX 모델을 현재 씬에 배치할 수 있습니다.

| 폴더 | 내용 |
|---|---|
| Assets | 이미지, 오디오, 도형 등 프로젝트 애셋 |
| Scripts | RiceMonkey 스크립트 소스 |
| Prefabs | 다시 사용할 오브젝트 구성 |
| Settings | 프로젝트 설정과 배포할 스크립트 모듈 |
| Temp | 임시 파일과 이전 형식 마이그레이션 백업 |

예제로 [Grass1000](../Projects/Grass1000/Grass1000.rice)과 [Steve](../Projects/Steve/Steve.rice)를 제공합니다. Rice에서 프로젝트를 열고 **Play**를 누릅니다. [Grass1000.exe](../Builds/Grass1000.exe)와 [Steve.exe](../Builds/Steve.exe)는 즉시 실행하거나 RiceCon에 등록할 수 있는 완성 예제입니다.

## 게임 빌드와 송출

**Project → Project / Export → Build standalone game**에서 게임을 빌드합니다. 결과 EXE 하나를 복사하면 Windows x64 및 OpenGL 3.3을 지원하는 PC에서 실행할 수 있습니다. 스크립트를 사용하는 게임에는 저장 시 컴파일된 RiceMonkey 모듈이 포함됩니다.

설치 폴더에서 다음과 같이 실행할 수도 있습니다.

```powershell
.\Rice.exe ".\Projects\Steve\Steve.rice"
.\Rice.exe --project ".\Projects\Steve\Steve.rice" --export ".\Builds\Steve Demo.exe"
.\RicePlayer.exe ".\Builds\Steve Demo.exe"
.\RiceCon.exe --build ".\Builds\Steve Demo.exe"
```

RiceCon에서 **방송 슬롯 추가**를 누르고 빌드를 배정합니다. **관리 → 방송 설정**에서 YouTube Studio의 RTMPS 서버 주소와 스트림 키를 저장합니다. **게임 실행 → 송출 시작**으로 영상과 게임 오디오를 보내고 **송출 종료**로 중지합니다. 송출에는 현재 NVIDIA NVENC를 지원하는 GPU가 필요합니다. 예약 방송의 공개 시작·종료는 YouTube Studio의 방송 설정을 따릅니다. [RiceCon 안내](Tools/RiceCon/README.md) · [YouTube 인코더 방송 안내](https://support.google.com/youtube/answer/2907883?hl=ko)

## 엔진 개발

`Engine`에 소스, SDK, 도구, 문서와 런타임 DLL이 있습니다. Visual Studio C++ 도구, Windows SDK, CMake, vcpkg와 FMOD SDK를 준비한 뒤 설치 폴더에서 빌드합니다.

```powershell
$env:VCPKG_ROOT = 'C:/path/to/vcpkg'
.\Engine\Build.ps1 -FmodRoot 'C:/Program Files (x86)/FMOD SoundSystem/FMOD Studio API Windows'
```

중간 빌드는 `Engine/Cache`에 둡니다. 테스트를 마치면 설치 폴더의 실행 파일과 `Engine/Runtime`을 갱신합니다. 기존 의존성은 `-InstalledDependencies`로 재사용할 수 있습니다.

Rice와 RicePlayer는 `Resources/Icons/Rice.ico`를, RiceCon은 `Resources/Icons/RiceCon.ico`를 사용합니다. 빌드할 때 각 아이콘을 실행기와 런타임 EXE에 내장하므로 설치 폴더를 다른 PC로 복사해도 탐색기와 앱 창에 같은 로고가 표시됩니다.

Rice와 RiceCon의 UI에는 넥슨이 제공한 메이플스토리 Light 서체를 사용합니다. 두 앱의 런타임에 원본 서체와 저작권 안내가 내장되어 다른 PC에서도 글꼴을 별도로 설치할 필요가 없습니다. [서체 사용 조건](ThirdParty/Maplestory/LICENSE.txt)

[사용 설명서](docs/EngineManual.md) · [엔진 API](docs/EngineFunctions.md) · [섀도맵 구현](docs/ShadowMaps.md)
