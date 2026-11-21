---
name: echo-performance-qa
description: Run Echo Assault's 1080p crowd benchmark or audit its existing frame captures. Use for performance reruns, viewport mismatches, and handoffs that must distinguish the latest attempt from the latest valid measurement.
---

# Echo Assault 성능 QA

- 새 1080p 측정 요청에는 저장소 루트에서 `Tools/PerfCheck.ps1`을 실행한다. 실행 환경은 README의 1080p 전투 성능 측정 절차를 따른다. 기존 결과 검토에서는 아티팩트를 읽는다. `PerfCheckReport.py --case`와 `--report`는 JSON에 쓰므로 읽기 전용 요청에서는 원본과 집계 파일을 직접 대조한다.
- 사용자가 지정한 폴더 범위에서 실행을 비교한다. 새 실행은 명령이 출력한 결과 경로에서 `run.json`의 상태와 회차 수를 확인한다. 패키징이나 실행이 실패하면 `package.log` 또는 회차 로그에서 최초로 실패한 단계, 오류와 새 캡처 수를 확인한다. 이전 유효 측정은 별도 실행 ID와 과거 결과라는 표시를 붙여 인용한다.
- 1080p 판정에는 회차별 CSV의 뷰포트 값과 실행별 `GameUserSettings.ini`를 확인한다. 시작 인자는 요청값, 모니터 로그는 디스플레이 설정의 근거다. 해상도가 다르면 해당 측정을 1080p 비교에서 제외한다. 과거 캡처의 해상도 증거가 저장 설정까지라면 그 범위를 명시한다.
- 정규 측정은 이동·정지 각각 세 번이다. 원본 회차와 집계를 대조해 1920×1080, 시작 적 500 중 원거리 100, 모든 무기 3단계, 측정 전 세 분신의 공격 재생, 게임 시간 210초부터 240초 직전까지의 회차당 1,800프레임을 확인한다. 측정이 유효하면 p95와 16.67ms 예산 초과 프레임을 보고한다. 유효성 판정과 성능 예산 판정은 각각 표기한다.
- 결론에 해당 실행 ID와 유효한 회차 수를 넣고, 원본 로그나 CSV와 집계 경로를 인용한다.
