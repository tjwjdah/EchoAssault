# 검증 근거

## 1080p 최종 웨이브

`PerfCheck-20261120-205404-312`의 이동·정지 각 3회입니다. i9-12900KF·RTX 3050, UE 5.5.3, DX12 오프스크린 Development 패키지에서 측정했습니다. 전투 시작은 근접 400·원거리 100마리입니다. 모든 무기를 3단계로 설정하고 세 분신의 발사 재생을 확인한 뒤 게임 시간 210초 이상 240초 미만을 측정했습니다. 적 처치와 보충으로 표본의 실제 생존 적은 달라집니다.

- [PerfCheck-Frames.csv](PerfCheck-Frames.csv) — 각 회차에서 `sample` 1,800행과, 직전에 세 분신의 공격 재생을 확인한 `warmup` 1행을 뽑았습니다. 각 프레임에 실제 뷰포트와 무기·분신·적 카운터가 들어 있습니다.
- [PerfCheck-Summary.json](PerfCheck-Summary.json) — 저장 해상도와 실제 뷰포트, 회차별 p95, 60 FPS 시간 예산 초과 수와 추가 측정값입니다. 공개용 CSV를 가리키도록 캡처 경로를 바꿨습니다. 원본 패키지의 내부 실행 파일 SHA-256을 확인했습니다.
- [PerfCheck-BuildInputs.json](PerfCheck-BuildInputs.json) — 당시 `Source`, `Config`, `Content`, `EchoAssault.uproject` 22개 파일의 패키징 전후 지문이 일치했습니다. 공개 스냅샷과 비교하면 `DefaultEngine.ini`의 로컬 Android File Server 설정값 한 줄을 정리했고 나머지 21개 파일의 지문이 같습니다.

다시 계산하려면 CSV에서 회차별 `sample` 행의 `frame_ms`를 오름차순으로 정렬합니다. 1부터 세어 `ceil(0.95 × 1800)`번째가 p95입니다. 회차별 p95 세 값의 중앙값을 비교하고, `1000 / 60` ms보다 긴 프레임을 셉니다. 새 실행의 실제 유효성 기준은 [PerfCheckReport.py](../../Tools/PerfCheckReport.py)에 있습니다.

## 탄환 ISM 격리 측정

`IsmApiAB-20261119-130601`에서 발사량 40·160발/초마다 개별 호출과 일괄 호출을 세 쌍 비교했습니다. 각 반복·발사량에 360표본을 사용했습니다. `apply_ms`는 샘플 내부의 `EchoProjectileApplyTransforms` 이벤트 시간입니다. `scope_ms`는 발사 준비와 월드 Tick을 포함한 테스트 범위입니다.

| 160발/초 첫 쌍 평균 | 개별 호출 | 일괄 호출 |
| --- | ---: | ---: |
| 위치 적용 구간 | 0.349 ms | 0.026 ms |
| 테스트 CPU 범위 | 2.534 ms | 2.216 ms |

이 수치는 CPU 타이머를 분리해 관찰한 같은 테스트 내 비교입니다. [ISM-Samples.csv](ISM-Samples.csv)의 `mode`·`run`·`shots_per_s`로 표본을 나누면 각 평균을 재계산할 수 있습니다. 세 쌍의 통계와 중앙값은 [ISM-Comparison.json](ISM-Comparison.json)에 있습니다.

## QA 판정 발췌

[QA-Case-Evidence.json](QA-Case-Evidence.json)은 정상 측정과 새 세션 재실행의 집계, 888×500 저장 해상도, 스테이징 실패 사유 및 실행 수를 원본에서 확인해 뽑은 값입니다. 원본 로그에 포함된 머신 경로, 계정 및 실행 환경 출력은 공개 발췌에서 제외했습니다. [판정 기록](../Status.md#인수인계와-qa-사례)

## 공개 스냅샷 재구성

[Snapshot-Verification.json](Snapshot-Verification.json)은 공개 스냅샷의 별도 복사본에서 수행한 네이티브 리플레이, Unreal 자동 테스트, Win64 Shipping 패키징의 결과를 담고 있습니다.
