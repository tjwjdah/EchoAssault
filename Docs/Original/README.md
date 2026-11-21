# Echo Assault

> **개발 당시 상세 문서 보관본** · 원본 파일 `README.md` · 2026-11-21 KST 개발 작업 트리 기준.
> 실행 절차, 트러블슈팅, 하네스 도입 판단과 다음 세션 시작 방법을 기록한다. 명령 예시와 당시 소스 ZIP·패키지 참조는 이 문서가 쓰인 시점의 개발 환경을 설명한다.
> 공개 시 정리한 항목은 [익명화 기록](Redaction-Notes.md)을 참고한다. [공개 README](../../README.md)

4분 동안 드론으로 생존하는 3D 탑다운 액션. 8초 전의 자신이 같은 장소에서 같은 방향으로 다시 쏜다. 과거의 발사를 이용해 지금 추적해 오는 적을 처치할 수 있다.

## 첫 실행 · Windows / UE 5.5.3

1. 현재 작업 폴더는 **프로젝트 루트**다. EchoAssault.uproject와 FirstPlay.cmd가 보이는 이 폴더에서 편집·빌드·실행한다. 에디터에 열려 있는 다른 위치의 동명 프로젝트는 실행 결과로 혼동하지 않는다.
2. **FirstPlay.cmd**를 더블클릭한다. UE 5.5 기본 설치 경로와 Visual Studio 2022 C++ 도구를 사용한다. 이 버튼은 Editor 빌드 → 네이티브 잔상 검사 → 필요한 3D 에셋·맵 임포트 → UE Automation → 창 모드 게임 실행 순서로 진행한다. 첫 빌드에는 시간이 걸린다. 최초 실행 시 콘텐츠 생성이 필요하다.
3. 아레나가 보이면 WASD로 이동한다. 마우스를 적 쪽으로 움직이면 자동 사격한다. 적이 떨어뜨린 민트색 결정을 주우면 경험치가 오른다. 8초가 지나면 보라색 분신이 자신의 과거 경로를 따라가며 발사한다. 반투명 보라색 선은 첫 분신이 따라올 최근 경로이며 항상 표시된다. 시간 가속에 따라 실제 재생 지연만큼 8→7→6초로 줄어든다. 레벨업 메뉴에서 강화 한 개를 클릭한다.
4. 첫 검증은 바닥 모서리를 돌아 8초 뒤 분신과 위치·방향을 비교하는 것이다. 그다음 분신 탄환이 현재 적에게 명중하는지, 레벨업 창에서 적과 탄환이 멈추는지 확인한다. 데이터와 화면이 다르면 Docs/Status.md에 관찰 내용을 적는다.

### 창이 열리지 않을 때

명령 창의 마지막 오류 문장과 Saved/Logs 폴더에서 방금 만들어진 로그 이름을 확인한다. 로그 자체가 없으면 그 사실을 기록한다.

- **Live Coding is active** — EchoAssault 에디터에서 PIE를 멈추고 작업을 저장한 뒤 그 에디터와 게임을 닫는다. FirstPlay.cmd를 다시 실행한다. 다른 프로젝트는 닫지 않는다. 에디터를 유지하는 코드 반복 작업은 Ctrl+Alt+F11을 쓰지만, 이 전체 빌드·자동 검사 흐름에서는 EchoAssault를 닫고 진행한다.
- **Native replay test failed** — Saved/Checks/run-*의 vs-setup.log, environment.log, compile.log, test.log, exit-code.txt를 확인한다. 앞 단계가 실패하면 뒤 단계 로그는 생기지 않는다. 이전 EXE는 사용하지 않고 실행마다 현재 소스를 새 폴더에 다시 컴파일한다.

- **Editor build failed** — UBT-EchoAssaultEditor.log에서 최초 error를 확인한다. 다른 위치에 설치한 엔진은 PowerShell에서 아래 EngineRoot 인자로 지정한다.
- **No completed Unreal asset import** — Epic Games Launcher의 라이브러리에서 UE 5.5를 실행한다. 프로젝트 선택 창의 찾아보기에서 EchoAssault.uproject를 연다. 시작 맵이 없다는 알림이 나오면 빈 레벨에서 상단 Tools → Execute Python Script로 Tools/ImportArena.py를 실행한다. 메뉴가 없으면 Edit → Plugins의 Python Editor Script Plugin을 확인한다. Content Browser의 Content/Echo에 Arena, Scout, Hunter, Lancer, Crystal, Hull이 생겨야 한다. Arena를 열고 Play로 바로 확인할 수 있다. 자동 검사 재실행 전 에디터를 닫는다.
- **Automation report missing / failed** — Saved/Logs/Tests-*.log와 Saved/TestReports 아래 최신 index.json을 확인한다. 에디터의 Tools → Test Automation에서 EchoAssault.Replay.Timeline을 실행해 비교한다. 사람이 직접 플레이할 때는 Arena를 열고 상단 Play를 누른다.
- **Unreal returned without creating its test log** — 런타임 검사가 실행됐다고 확인할 근거가 없다. Saved/Logs/Verify-*-result.json의 Stage/Error와 Verify-*-error.log를 확인한다. 현재 Work 셸에서 발생한 상태이며, 일반 Windows 세션에서 FirstPlay.cmd 실행 결과와 비교해야 한다. Windows 보안 설정을 임의로 변경하지 않는다.

### 각각 실행하기

프로젝트 폴더의 빈 공간에서 우클릭 → 터미널에서 열기를 선택한다. PowerShell에서 아래 명령을 필요한 것만 실행한다.

~~~powershell
.\Tools\ReplayCheck.cmd
.\Tools\Build.ps1
.\Tools\Build.ps1 -Game
.\Tools\Verify.ps1 -Play
.\Tools\Verify.ps1 -EngineRoot '<UE 5.5 설치 폴더>' -Play
~~~

Build.ps1은 C++ 타깃을 컴파일한다. 배포용 게임에는 콘텐츠 쿠킹·스테이징이 추가로 필요하다. 에셋 생성과 플레이 검증 후 UE 에디터 Platforms → Windows → Package Project로 진행하고 결과 폴더를 다른 위치에서 실행해 확인한다.

### Windows 패키지 실행

1. `Docs/Status.md`에서 최신 `Artifacts/Windows-Shipping-*` 폴더를 찾는다. 다른 위치나 PC에서 실행할 때는 `EchoAssault.exe`, `EchoAssault`, `Engine`이 같은 위치에 있도록 폴더 전체를 복사한다.
2. Windows에서 폴더 최상단의 `EchoAssault.exe`를 실행한다. 런타임 설치가 필요하면 `Engine/Extras/Redist/en-us/UEPrereqSetup_x64.exe`를 실행한 뒤 다시 연다.
3. 아레나와 한글 체력·레벨 HUD가 보이면 시작 확인을 마친다. F6 레벨업이 필요한 개발 진단은 `Docs/Status.md`에 기록된 Development 패키지의 `InputDebug.cmd`에서 실행한다.

Verify.ps1은 `EchoAssault` 필터로 Timeline, SpawnOverlap, CurrentWorldCombat, ProgressionAndOutcomes **4개 테스트 각각의 Success**를 요구한다. 파일 존재나 프로세스 종료 0만으로 성공하지 않는다. 최신 검증 상태·남은 수동 검사는 Docs/Status.md를 따른다.

### 1080p 전투 성능 측정

UE 5.5, Git과 PATH에서 실행할 수 있는 Python 3가 필요하다. 에디터와 게임을 닫고 PowerShell에서 `.\Tools\PerfCheck.ps1`을 실행한다. 엔진 위치가 다르면 `-EngineRoot`로 지정한다. 현재 작업 트리에서 Development 패키지를 만들고, 1080p에서 이동과 정지를 각각 세 번 번갈아 실행한다.

시작 적은 근접 400·원거리 100마리다. 분신 세 기가 모두 공격을 한 뒤 게임 시각 3:30부터 4:00 직전까지 프레임을 수집한다. `Saved/Profiles/PerfCheck-*/summary.json`에 평균 FPS, p95, 16.67ms 초과 프레임, 실제 생존 적 수를 남긴다. 실행별 로그와 CSV는 같은 폴더에 보존한다. 해상도나 재생 조건이 어긋나면 해당 실행의 `result.json`에 이유를 기록한다.

패키징 전과 성공 직후에 `Source`, `Config`, `Content`, `EchoAssault.uproject`의 파일 경로와 내용 SHA-256을 각각 기록한다. `run.json`과 `summary.json`의 `build_inputs`에서 비교 결과(`Stable` 또는 `Changed`)와 스냅샷 파일을 확인한다. 이 판정은 두 시점의 프로젝트 입력 비교이며 성능 측정 유효성과 별도로 기록된다. 현재 입력 해시만 확인하려면 `python .\Tools\PerfCheckReport.py --build-inputs .`를 실행한다.

Codex에 성능 측정이나 결과 검토를 맡길 때는 `$echo-performance-qa`를 사용한다. 스킬 파일은 `.agents/skills/echo-performance-qa/SKILL.md`에 있다.

## Visual Studio 솔루션 열기

현재 빌드는 UnrealBuildTool을 직접 호출하므로 `.sln` 없이도 동작한다. Visual Studio의 솔루션 탐색기로 전체 프로젝트를 보려면 `EchoAssault.uproject` 우클릭 → Windows 11에서는 추가 옵션 표시 → Generate Visual Studio project files를 실행한다. 생성된 `EchoAssault.sln`을 더블클릭한다. 솔루션 탐색기에서 Games → EchoAssault → Source → EchoAssault로 이동하면 C++ 파일을 볼 수 있다.

`.sln`과 `Intermediate/ProjectFiles`는 소스에서 재생성하는 로컬 파일이므로 Git에서 제외한다. Work에서 생성할 때 엔진 설치 폴더의 `Engine/Intermediate/ProjectFiles/PrimaryProjectName.txt` 쓰기가 차단될 수 있다. 이 경우 성공으로 기록하지 않고 일반 Windows 세션의 위 생성 메뉴로 진행한다. 게임 소스나 프로젝트를 새로 만들 필요는 없다.

## AI와 개발하는 방식

작업자는 `AGENTS.md`의 규칙을 읽고 `Docs/Status.md`에서 현재 문제·완료 조건을 찾는다. 설계 이유가 필요할 때 `Docs/Design.md`와 관련 소스를 읽는다. 필요한 정보만 문서에서 골라 전달하는 것이 이 프로젝트의 컨텍스트 관리다. 모든 대화를 새 세션에 복사할 필요는 없다.

| 문서 | 쓰는 이유 | 개발자가 직접 확인하는 방법 |
|---|---|---|
| AGENTS | AI의 수정 범위·검증·보고 규칙 유지 | 실제 변경 파일과 지침을 비교한다. 핵심 판단은 사람이 하고 일반 수정은 위임했는지 확인한다. |
| Design | 게임 규칙과 구현 선택의 이유 유지 | 기록 이벤트의 과거 Rank와 현재 적 피해를 소스에서 따라가 본다. |
| Status | 현재 문제·완료 여부·다음 한 작업 유지 | 최신 보고서의 테스트 결과·커밋과 표를 대조한다. 계획과 결과를 혼동하지 않는다. |
| README | 실행·운영 절차 재현 | 같은 폴더에서 검증 명령을 실행해 실제 로그와 보고서가 나오는지 확인한다. |

하네스는 이 문서와 도구를 연결한 작업 절차다. `작업·완료 조건 → 코드 수정 → 빌드·관련 검사 → 실패 원인 확인 → 재수정 → 근거와 다음 작업 저장`을 반복한다. 이 흐름에서 AI는 코드와 로그를 다루고, 개발자는 중요한 설계·검증 범위와 실제 게임 결과를 판단한다.

`Verify.ps1`은 다음 순서로 동작한다.

1. 같은 루트의 Editor 타깃을 빌드한다. 컴파일 실패·Live Coding 충돌을 통과시키지 않는다.
2. `ReplayCheck.cmd`가 최신 C++를 고유 폴더에 재컴파일하고 재생 시계·이벤트를 검사한다. 환경/컴파일/실행 종료 코드를 보존한다.
3. 필요한 에셋만 임포트하고 6개 필수 콘텐츠를 확인한다.
4. Unreal `EchoAssault` 테스트를 실행한다. 새 로그·새 보고서를 요구하고 CheckAutomationReport.ps1에서 필수 테스트 4개의 개별 Success를 모두 확인한다. 실패한 테스트의 조건을 함께 출력한다.
5. 결과 JSON에 마지막 단계·성공 여부·오류를 남긴다. 모두 통과한 경우에만 `-Play`가 게임을 연다. 사람이 플레이한 결과는 별도로 기록한다.

프로세스 종료 0만 확인하는 것보다 실제 보고서까지 확인해야 실행되지 않은 검사를 발견할 수 있다. 테스트 실패도 게임 결함과 검사 환경 결함을 나눠 조사한다. 기대값을 낮추거나 검사를 제거해 성공으로 만들지 않는다.

### 도구 도입 판단

| 기술 | 역할·Echo Assault 활용 | 비용·현재 판단 | 확인 방법 |
|---|---|---|---|
| Work | 요구·작업·근거를 묶어 개발 진행 | 현재 사용. 대화만으로 상태를 관리하면 인수인계가 어렵다. | 로컬 문서·수정·로그가 실제로 생겼는지 확인 |
| Codex 코딩 에이전트 | 관련 소스 분석·수정·빌드 위임 | 현재 세션이 같은 루트에서 수행. 별도 Codex CLI 연결·사용은 확인된 것과 구분 | 아래 인계 지시문으로 파일 읽기→작은 수정→검사→diff 확인 |
| Git/GitHub | 복구 지점·코드 공유 | 로컬 체크포인트 사용. 원격 생성·업로드·Push는 사용자 확인 필요 | status/diff/log 확인, 승인 후 원격 커밋 대조 |
| CI | 커밋마다 반복 검사 | 현재 로컬 Verify가 우선. 원격 Unreal 실행 환경 구축·유지 비용이 있어 MVP 뒤 검토 | 추후 같은 커밋에서 동일 검사 결과·실패 코드 비교 |
| Skills | 반복 작업의 절차 재사용 | 저장소의 `echo-performance-qa`에 1080p 검사 선택과 원본 판정 절차를 모음 | 정상 실행·해상도 불일치·최근 실패 사례의 판정과 증거를 비교 |
| Hooks | 커밋 등 사건에 맞춰 자동 실행 | 무거운 Unreal 빌드를 매 커밋에 강제하면 지연. 현재 수동 Verify 사용 | 도입 시 의도적 오류 커밋 차단·우회 여부 확인 |
| MCP | 외부 데이터·도구 연결 | GitHub 읽기 연결 활용. 로컬 게임 빌드는 기존 셸이면 충분 | 실제 응답과 로컬 원격 설정을 각각 확인 |
| Tool Calling | AI가 파일·빌드·검사 도구 호출 | 현재 작업의 실행 수단. 성공 응답과 기능 성공은 구분 | 도구 결과의 파일·종료 코드·실제 보고서 확인 |
| 에이전트 오케스트레이션 | 독립 작업을 여러 작업자에 분배 | 별도 컨텍스트 인계 실험 수행. 작은 수정은 단일 작업자가 효율적 | 수정 범위 충돌·빌드 동시 실행·추가 설명 필요 여부 기록 |

이미 수행한 의도적 컴파일/실행 실패 검사는 Status에 근거가 있다. 검사 스크립트나 실패 처리 경로가 바뀌었을 때만 관련 검사를 다시 한다. 매번 전체 환경·기존 실험을 재구축하지 않는다.

### 사용자 코드 리뷰 예시

실제 버그를 고칠 때 `증상 → 관련 함수 → 엔진 API의 실제 동작 → 수정 → 테스트 근거`를 함께 확인한다. 이번 충돌 테스트에서는 `EchoGameplayTests.cpp`의 구체 크기 설정을 검토한다. `InitSphereRadius`는 물리 상태를 갱신하지 않고, `SetSphereRadius`는 등록된 충돌 형태를 갱신한다. 생성자 초기화와 월드 등록 후 변경에 같은 함수를 써도 되는지 설명해 보는 것이 짧은 리뷰 과제다. 사용자가 실제로 답하거나 수정한 뒤에만 참여 완료로 기록한다.

## Codex와 다음 세션

이 폴더가 AI 작업 공간이다. 이번 Work 세션에서 실제 .cpp 수정과 UBT 실행을 확인했다. 별도 CLI를 설치할 필요 없이 같은 프로젝트를 다시 열어 다음과 같이 요청할 수 있다.

> EchoAssault/AGENTS.md와 Docs/Status.md를 읽고, 최근 커밋과 빌드 결과를 확인해. 현재 체크리스트의 미완료 항목 중 우선순위가 높은 로컬 작업부터 이어가고, 이미 확인한 MVP 플레이를 반복 요구하지 마. 수정 후 실행한 검사와 남은 문제를 기록해.

Git 체크포인트는 배포한 소스 ZIP의 로컬 저장소에 포함한다. 압축을 푼 뒤 git status -sb, git log -1을 실행하면 현재 브랜치와 체크포인트를 확인할 수 있다. 이후 수정 시 의도한 파일만 스테이징해 커밋한다. 검증을 통과한 Content/Echo의 .uasset과 .umap도 커밋한다. 생성되는 Binaries, Intermediate, Saved는 별도로 관리한다.

## 코드와 작업 기록

- Docs/Design.md — 확정 기획, 핵심 코드 흐름, 임시 수치와 기술 판단
- Docs/Status.md — 실제 검증, 오늘 남은 일, 19일·22일 합격 기준과 채용 준비
- Source/EchoAssault/EchoPlayback.h, EchoHistory.h — 공통 기록과 개별 재생 시계
- Source/EchoAssault/EchoGame.cpp, EchoActors.cpp, EchoInterface.cpp — 전투, 투사체, 성장 및 UI
- ArtSource, Tools/MakeMeshes.py, Tools/ImportArena.py — 직접 생성한 OBJ와 Unreal 임포트 절차
