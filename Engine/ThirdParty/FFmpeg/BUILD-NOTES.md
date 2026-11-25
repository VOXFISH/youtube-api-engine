# FFmpeg 빌드 및 배포

현재 미디어 어댑터는 Windows x64의 FFmpeg 8.1.1 및 NVIDIA NVENC를 대상으로 한다. `vcpkg.json`의 기존 baseline에 해당 버전이 고정되어 있다.

## 구성

- vcpkg 기능: `core,avcodec,avformat,swresample,swscale,nvcodec`.
- DLL 동적 연결. `gpl`, `nonfree`, `version3`, `x264`, `x265` 기능을 켜지 않는다.
- Windows TLS는 Schannel을 사용한다. RTMPS 출력은 인증서 검증을 요청한다.
- `auto` 영상 인코더는 선택한 코덱의 `h264_nvenc`, `hevc_nvenc`, `av1_nvenc`로 해석한다. 다른 인코더로 재시도하지 않는다. AAC 오디오는 FFmpeg 내장 AAC를 사용한다.
- 설치된 FFmpeg의 실제 구성과 라이선스 문자열을 배포 전 확인한다. 라이브러리 기능은 합쳐질 수 있으므로 다른 의존성이 GPL 기능을 추가하지 않았는지도 확인한다.

## 검증 구분

`EncoderSessions`는 실패 주입으로 새 세션 거부, 기존 세션 보존, 같은 영상 설정의 공유, 최대 5개 출력 제한을 검증한다.

`RiceHardwareEncoderTests`는 실제 GPU를 사용해 H.264/HEVC/AV1의 1080p 합성 프레임을 인코딩·디코딩하고 AAC와 로컬 FLV 파일로 결합한다. `RICE_TEST_HARDWARE_ENCODER=ON`일 때 CTest에 등록된다. 일반 CI는 GPU가 없을 수 있으므로 기본값은 OFF다. 실제 GPU 테스트 실패를 소프트웨어 인코더 성공으로 대체하지 않는다. 이 짧은 기능 검사는 5개 동시 1080p60의 지속 성능 시험을 대체하지 않는다.

2026-11-16 기준 PC에서 위 왕복 검사와 5개 독립 H.264 NVENC 세션의 동시 생성·단일 프레임 출력을 통과했다. 로컬 결과물은 `Engine/Cache/local/TestData/HardwareEncoder`에 생성한다. RTMPS 구현은 아직 실제 목적지 접속으로 검증하지 않았다.

## 배포 자료

배포 패키지에는 사용한 FFmpeg DLL, 라이선스·저작권 고지, 정확한 버전·빌드 옵션·패치를 기록하고 해당 DLL의 소스를 함께 제공하는 절차가 필요하다. `Engine/Cache/dependencies`는 재생성 가능한 다운로드·빌드 캐시이며 배포 자료의 영구 보관 위치로 사용하지 않는다. 재구성 시 저장소의 `Engine/vcpkg.json`의 고정 baseline `d015e31e90838a4c9dfa3eed45979bc70d9357fc`와 기능 목록을 사용한다. 해당 vcpkg FFmpeg 포트가 FFmpeg 8.1.1 소스와 포트의 패치를 다시 받아 적용한다.

현재 설치된 의존성은 `Engine/Cache/ninja-debug/vcpkg_installed`에 있다. `x64-windows/share/ffmpeg`의 `copyright`, `vcpkg_abi_info.txt`, `vcpkg.spdx.json`에서 고지와 포트·패치 식별 정보를 확인할 수 있다. 발표용 실행 패키지의 고지 문서는 `Engine/ThirdParty/FFmpeg`에 설치된다. 소스 아카이브와 패치는 자동으로 실행 패키지에 포함되지 않으므로, 출시 시에는 고정 포트로 재확보해 해당 DLL의 소스 제공 자료를 별도로 구성·검증한다. LGPL 라이브러리 교체 및 디버깅을 제한하지 않아야 한다.

참고: [FFmpeg 라이선스·준수 체크리스트](https://ffmpeg.org/legal.html), [FFmpeg send/receive API](https://ffmpeg.org/doxygen/trunk/group__lavc__encdec.html), [OBS Source/Encoder/Output 구조](https://docs.obsproject.com/backend-design).

