"""
2048 Game Core Logic

This module implements the core game mechanics of 2048.
It is designed to be reusable for:
  - Web gameplay (via API server)
  - AI agent training (import as a Python module)
"""

from __future__ import annotations

import copy
import random
from enum import IntEnum
from typing import List, Optional, Tuple

# Type alias
Board = List[List[int]]

BOARD_SIZE = 4


class Direction(IntEnum):
    UP = 0
    RIGHT = 1
    DOWN = 2
    LEFT = 3


class GameState:
    """Represents the full state of a 2048 game."""

    def __init__(self, board: Optional[Board] = None, score: int = 0):
        if board is not None:
            self.board = copy.deepcopy(board)
        else:
            self.board = [[0] * BOARD_SIZE for _ in range(BOARD_SIZE)]
        self.score = score

    def copy(self) -> "GameState":
        return GameState(board=self.board, score=self.score)

    def to_dict(self) -> dict:
        return {
            "board": self.board,
            "score": self.score,
            "game_over": is_game_over(self),
            "max_tile": max(max(row) for row in self.board),
        }


# ---------------------------------------------------------------------------
# Pure helper functions (operate on raw board / rows)
# ---------------------------------------------------------------------------


def _compress(row: List[int]) -> Tuple[List[int], int]:
    """
    Slide and merge a single row to the LEFT.
    Returns (new_row, points_earned).
    """
    # Remove zeros
    non_zero = [v for v in row if v != 0]
    merged: List[int] = []
    score = 0
    skip = False

    for i in range(len(non_zero)):
        if skip:
            skip = False
            continue
        if i + 1 < len(non_zero) and non_zero[i] == non_zero[i + 1]:
            merged_val = non_zero[i] * 2
            merged.append(merged_val)
            score += merged_val
            skip = True
        else:
            merged.append(non_zero[i])

    # Pad with zeros
    merged += [0] * (BOARD_SIZE - len(merged))
    return merged, score


def _rotate_cw(board: Board) -> Board:
    """Rotate the board 90 degrees clockwise."""
    n = len(board)
    return [[board[n - 1 - j][i] for j in range(n)] for i in range(n)]


def _rotate_ccw(board: Board) -> Board:
    """Rotate the board 90 degrees counter-clockwise."""
    n = len(board)
    return [[board[j][n - 1 - i] for j in range(n)] for i in range(n)]


# ---------------------------------------------------------------------------
# Core game operations
# ---------------------------------------------------------------------------


def move(state: GameState, direction: Direction) -> Tuple[GameState, bool]:
    """
    Apply a move in the given direction.

    Returns:
        (new_state, changed): new_state is the resulting GameState,
        changed indicates whether the board actually changed.
    """
    board = copy.deepcopy(state.board)

    # Rotate so that the target direction becomes LEFT, then compress,
    # then rotate back. We use CCW rotations to transform each direction
    # into a LEFT-compress problem:
    #   LEFT  -> 0 CCW rotations
    #   UP    -> 1 CCW rotation
    #   RIGHT -> 2 CCW rotations
    #   DOWN  -> 3 CCW rotations
    rotation_map = {
        Direction.LEFT: 0,
        Direction.UP: 1,
        Direction.RIGHT: 2,
        Direction.DOWN: 3,
    }
    rot = rotation_map[direction]

    for _ in range(rot):
        board = _rotate_ccw(board)

    total_score = 0
    new_board = []
    for row in board:
        merged_row, pts = _compress(row)
        new_board.append(merged_row)
        total_score += pts

    # Rotate back (CW to undo the CCW rotations)
    for _ in range(rot):
        new_board = _rotate_cw(new_board)

    changed = new_board != state.board
    new_state = GameState(board=new_board, score=state.score + total_score)
    return new_state, changed


def spawn_tile(state: GameState, rng: Optional[random.Random] = None) -> GameState:
    """
    Spawn a new tile (2 with 90% probability, 4 with 10%) on a random empty cell.
    Mutates nothing; returns a new GameState.
    """
    if rng is None:
        rng = random.Random()

    empty_cells = [
        (r, c)
        for r in range(BOARD_SIZE)
        for c in range(BOARD_SIZE)
        if state.board[r][c] == 0
    ]
    if not empty_cells:
        return state.copy()

    r, c = rng.choice(empty_cells)
    value = 2 if rng.random() < 0.9 else 4
    new_state = state.copy()
    new_state.board[r][c] = value
    return new_state


def new_game(rng: Optional[random.Random] = None) -> GameState:
    """Create a fresh game with two starting tiles."""
    state = GameState()
    state = spawn_tile(state, rng)
    state = spawn_tile(state, rng)
    return state


def get_available_moves(state: GameState) -> List[Direction]:
    """Return the list of directions that would change the board."""
    available = []
    for d in Direction:
        _, changed = move(state, d)
        if changed:
            available.append(d)
    return available


def is_game_over(state: GameState) -> bool:
    """Check if no moves are possible."""
    return len(get_available_moves(state)) == 0


def step(
    state: GameState, direction: Direction, rng: Optional[random.Random] = None
) -> Tuple[GameState, int, bool]:
    """
    Gym-like step function for AI training.

    Args:
        state: current game state
        direction: action to take
        rng: optional random generator for reproducibility

    Returns:
        (new_state, reward, done)
        - reward is the score gained from this move
        - done is True if the game is over after this move
    """
    old_score = state.score
    new_state, changed = move(state, direction)

    if not changed:
        return state.copy(), 0, is_game_over(state)

    new_state = spawn_tile(new_state, rng)
    reward = new_state.score - old_score
    done = is_game_over(new_state)
    return new_state, reward, done
