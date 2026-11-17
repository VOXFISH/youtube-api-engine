# Data 모듈 문서

[Data Routing 명세서](DataRouting.md)는 표준 C++ 등록 매크로·native 멤버/컨테이너 proxy, 데이터 전용 클래스, SQLite 저장, 슬롯 관리와 global 재접속 규칙을 정의한다.

게임 임시값은 메모리, slot/stored/global은 내장 SQLite를 사용한다. slot은 Controller의 방송 항목에 영속 저장되며 같은 인스턴스의 여러 출력은 주 슬롯 데이터를 공유한다. 별도 DB 서버 설치는 요구하지 않는다.

방송 설정·슬롯·송출 수명은 [YouTube 연동 및 스트리밍 명세서](../../docs/requirements/YouTubeStreaming.md), 사이클·명령 패턴·콜백은 [사이클 및 명령 명세서](../../docs/requirements/CycleAndCommands.md)를 따른다.

저장 기반과 native proxy의 구현·테스트가 추가되었으며 Player 및 송출 전체 통합은 진행 중이다. 최신 결정과 구현 경계는 명세서의 10~12절에 정리되어 있다.
