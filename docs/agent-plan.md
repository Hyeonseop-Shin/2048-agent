# 2048 Expectimax AI Agent

## Overview

Bitboard 표현 + Expectimax 탐색 + CMA-ES 최적화 휴리스틱 평가함수를 사용하여
2048 게임을 최적에 가까운 수준으로 플레이하는 에이전트.

**참고 구현**: [nneonneo/2048-ai](https://github.com/nneonneo/2048-ai)

## Performance (10-game simulation)

| Metric | Value |
|--------|-------|
| 2048 달성률 | 100% |
| 4096 달성률 | 100% |
| 8192 달성률 | 100% |
| 16384 달성률 | 90% |
| 평균 점수 | ~328,000 |
| 최고 점수 | ~387,000 |
| 게임당 소요 시간 | ~54초 |
| 수/초 | ~232 |

## Architecture

```
agent/
├── engine.c       # C 엔진 (bitboard, move tables, heuristics, expectimax)
├── build.py       # 빌드 스크립트 (cc → .dylib/.so)
├── ai.py          # Python ctypes 래퍼
├── simulate.py    # 시뮬레이션 러너 & 통계
└── __init__.py
```

### Bitboard Encoding

4×4 보드를 단일 `uint64_t`로 인코딩. 각 셀은 4비트(nibble)로 `log₂(tile_value)`를 저장.

```
Cell(r,c) → bit position = 4 × (4×r + c)
Row r     → bits [16×r .. 16×r + 15]
```

### Move Lookup Tables

4개의 사전 계산 테이블 (각 65,536 엔트리):

- `row_left_table[row]`: 행 왼쪽 이동의 XOR diff
- `row_right_table[row]`: 행 오른쪽 이동의 XOR diff
- `col_up_table[row]`: 열 위쪽 이동의 XOR diff
- `col_down_table[row]`: 열 아래쪽 이동의 XOR diff

이동 적용은 XOR 4번으로 완료됨.

### Heuristic Evaluation

CMA-ES로 최적화된 가중치:

| Heuristic | Weight | Description |
|-----------|--------|-------------|
| Merge potential | +700.0 | 인접 동일 타일 수 |
| Empty cells | +270.0 | 빈 셀 수 |
| Monotonicity | -47.0 (pow=4.0) | 단조성 위반 페널티 |
| Sum | -11.0 (pow=3.5) | 타일 랭크 합 페널티 |

행별 사전 계산 테이블 → 보드 평가 = 8번의 테이블 룩업.

### Expectimax Search

- **Max 노드**: 4방향 중 최고 기대값 선택
- **Chance 노드**: 빈 셀마다 2(90%) / 4(10%) 스폰 기대값
- **확률 가지치기**: `cprob < 0.001`이면 탐색 중단
- **적응적 깊이**: `max(3, distinct_tiles - 2)`, 최대 5
- **전이 테이블**: 중복 상태 캐싱 (4M 엔트리)

## Usage

### Build C Engine

```bash
uv run python -m agent.build
```

### Run Simulation

```bash
uv run python -m agent.simulate --games 10 --seed 42
```

### Web UI (AI Auto-Play)

```bash
uv run uvicorn server:app --host 0.0.0.0 --port 8000
```

http://localhost:8000 에서 "AI Play" 버튼 클릭.

### Python API

```python
from agent.ai import find_best_move

board = [[2, 4, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 2, 0]]
direction = find_best_move(board)  # 0=UP, 1=RIGHT, 2=DOWN, 3=LEFT
```
