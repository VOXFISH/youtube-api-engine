# 수동 성능 비교 도구

성능 비교용 소스는 이 폴더에 모아 두며 기본 빌드 및 CTest 실행에는 포함하지 않습니다. 필요한 대상을 명시해 빌드합니다.

```powershell
cmake --build Build/logging --config Release --target RiceInstancingBenchmark RiceSceneSpatialBenchmark RiceShadowBenchmark RiceShadowInvalidationBenchmark --parallel 4
```

측정 결과와 미리보기는 `Build/benchmarks/`에 저장합니다. 기존 분석은 `docs/benchmarks/`에 있으며, 엔진의 진단 기능과 회귀 테스트는 각각 `Engine/`과 `Tests/`에서 유지합니다.
