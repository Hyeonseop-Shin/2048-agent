# 2048-agent

2048 게임 환경 + Expectimax AI 에이전트. 웹 브라우저에서 직접 플레이 or AI 자동 플레이 가능

## Requirements

- Python 3.11+
- C 컴파일러 (macOS: Xcode CLT, Linux: gcc)
- [uv](https://docs.astral.sh/uv/) (Python 패키지 매니저)

## Installation

```bash
git clone https://github.com/Hyeonseop-Shin/2048-agent.git
cd 2048-agent

# 의존성 설치
uv sync

# C 엔진 빌드
uv run python -m agent.build
```

## Usage

### 1. Play on Web

```bash
uv run uvicorn server:app --host 0.0.0.0 --port 8000
```

브라우저에서 http://localhost:8000 접속.

- **방향키** (←↑→↓) 또는 **스와이프**로 타일 이동
- **New Game** 버튼으로 새 게임 시작

### 2. AI Auto-Play (Web)

위와 같이 서버를 실행한 후, 웹 페이지에서:

1. **AI Play** 버튼 클릭 — 에이전트가 자동으로 플레이 시작
2. **Speed 슬라이더** — 수 사이의 딜레이 조절 (5ms ~ 1000ms)
3. **Stop** 버튼 — 자동 플레이 중단

에이전트가 선택한 방향이 화살표(↑↓←→)로 실시간 표시됨.

### 3. Simulation (Terminal)

AI 에이전트로 여러 판을 자동 플레이하고 통계를 확인.

```bash
# 10판 시뮬레이션 (기본값)
uv run python -m agent.simulate

# 옵션 지정
uv run python -m agent.simulate --games 50 --seed 42
```

**옵션:**

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `--games`, `-n` | 플레이할 게임 수 | 10 |
| `--seed`, `-s` | 랜덤 시드 (재현 가능) | 시간 기반 |

**출력 예시:**

```
Game   1: score= 377048  max_tile=16384  moves=14305  time=61.9s
Game   2: score= 374232  max_tile=16384  moves=14176  time=60.6s
...

============================================================
SIMULATION RESULTS
============================================================
Score:
  Mean:         328,646
  Median:       365,216

Tile achievement rates:
  16384:   9/10  ( 90.0%)
   8192:  10/10  (100.0%)
   4096:  10/10  (100.0%)
   2048:  10/10  (100.0%)
============================================================
```

### 4. Python API

게임 로직과 AI를 직접 코드에서 사용 가능

```python
from game.core import new_game, step, Direction, is_game_over
from agent.ai import find_best_move

# 게임 생성
state = new_game()

# AI가 최적의 수 선택
direction = find_best_move(state.board)  # 0=UP, 1=RIGHT, 2=DOWN, 3=LEFT

# 수 실행
new_state, reward, done = step(state, Direction(direction))
```

## Project Structure

```
2048-agent/
├── game/
│   ├── core.py            # 게임 코어 로직 (Python)
│   └── __init__.py
├── agent/
│   ├── engine.c           # AI 엔진 (C, bitboard + expectimax)
│   ├── build.py           # C 엔진 빌드 스크립트
│   ├── ai.py              # Python ctypes 래퍼
│   ├── simulate.py        # 시뮬레이션 러너
│   └── __init__.py
├── static/
│   ├── index.html         # 웹 UI
│   ├── style.css          # 스타일
│   └── game.js            # 프론트엔드 로직
├── server.py              # FastAPI 서버
├── docs/
│   └── agent-plan.md      # AI 에이전트 기술 문서
├── pyproject.toml
└── uv.lock
```

## AI Agent Performance

10판 시뮬레이션 결과:

| 타일 | 달성률 |
|------|--------|
| 2048 | 100% |
| 4096 | 100% |
| 8192 | 100% |
| 16384 | 90% |

- 평균 점수: ~328,000
- 게임당 소요 시간: ~54초

기술적 상세는 [docs/agent-plan.md](docs/agent-plan.md) 참고.

## REST API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | 웹 게임 페이지 |
| `/api/new` | POST | 새 게임 시작. `{"seed": 42}` (선택) |
| `/api/move` | POST | 수동 이동. `{"direction": 0}` (0=UP, 1=RIGHT, 2=DOWN, 3=LEFT) |
| `/api/agent/move` | POST | AI가 최적의 수를 선택하여 이동 |
| `/api/state` | GET | 현재 게임 상태 조회 |
