# 발표용 테스트씬

에디터와 빌드 실행 파일은 저장소 최상단에 있습니다.

1. [RiceEditor.exe](../RiceEditor.exe)를 직접 실행합니다.
2. **File → Open Project**에서 `Grass1000/project.rice` 또는 `Steve/project.rice`를 선택합니다.
3. **Game → Play**로 장면을 실행합니다.

[최상단 RicePlayer.exe](../RicePlayer.exe)를 더블클릭하면 Steve 장면이 바로 재생됩니다. 에디터에서 다른 프로젝트를 연 뒤 Export하면 해당 프로젝트의 독립 실행 빌드를 만들 수 있습니다.

```text
저장소 최상단/
├─ RiceEditor.exe
├─ RicePlayer.exe
├─ RiceController.exe
├─ 실행에 필요한 DLL
└─ Presentation/
   ├─ Grass1000/  project.rice · main.rice · Assets
   └─ Steve/      project.rice · main.rice · Assets · Recordings
```

[발표 영상](Steve/Recordings/Steve-Shadow-Demo-1080p60.mp4): 13초 / 1080p / 60fps / 무음.

장면을 옮길 때 `.meta` 파일을 포함한 Assets 폴더를 함께 보관합니다. 전체 실행 패키지를 옮길 때는 최상단 실행 파일·DLL과 이 Presentation 폴더를 함께 복사합니다.
