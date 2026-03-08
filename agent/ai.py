"""
Python wrapper for the 2048 AI C engine via ctypes.

Usage:
    from agent.ai import find_best_move

    # board_2d is a 4x4 list of ints (actual tile values: 0, 2, 4, 8, ...)
    direction = find_best_move(board_2d)  # returns 0=UP, 1=RIGHT, 2=DOWN, 3=LEFT
"""

import ctypes
import sys
import os

_engine = None


def _get_lib_path():
    agent_dir = os.path.dirname(os.path.abspath(__file__))
    if sys.platform == "darwin":
        suffix = "dylib"
    elif sys.platform == "win32":
        suffix = "dll"
    else:
        suffix = "so"
    return os.path.join(agent_dir, f"engine.{suffix}")


def _load_engine():
    global _engine
    if _engine is not None:
        return _engine

    lib_path = _get_lib_path()
    if not os.path.exists(lib_path):
        # Auto-build if not found
        from agent.build import build

        build()

    _engine = ctypes.CDLL(lib_path)
    _engine.init_tables()

    # Declare function signatures
    _engine.find_best_move.argtypes = [ctypes.c_uint64]
    _engine.find_best_move.restype = ctypes.c_int

    _engine.execute_move.argtypes = [ctypes.c_uint64, ctypes.c_int]
    _engine.execute_move.restype = ctypes.c_uint64

    _engine.score_toplevel_move.argtypes = [ctypes.c_uint64, ctypes.c_int]
    _engine.score_toplevel_move.restype = ctypes.c_float

    _engine.get_tile.argtypes = [ctypes.c_uint64, ctypes.c_int]
    _engine.get_tile.restype = ctypes.c_int

    # Full game simulation in C
    _engine.play_game.argtypes = [ctypes.c_uint64]
    _engine.play_game.restype = None

    _engine.get_result_score.argtypes = []
    _engine.get_result_score.restype = ctypes.c_int32

    _engine.get_result_max_tile.argtypes = []
    _engine.get_result_max_tile.restype = ctypes.c_int32

    _engine.get_result_moves.argtypes = []
    _engine.get_result_moves.restype = ctypes.c_int32

    _engine.get_result_board.argtypes = []
    _engine.get_result_board.restype = ctypes.c_uint64

    return _engine


def board_to_bitboard(board_2d):
    """
    Convert a 4x4 Python board (actual tile values) to a C bitboard (log2 encoding).

    Args:
        board_2d: List[List[int]] — 4x4 board with values like 0, 2, 4, 8, ...

    Returns:
        int — 64-bit bitboard
    """
    bb = 0
    for r in range(4):
        for c in range(4):
            val = board_2d[r][c]
            if val > 0:
                rank = val.bit_length() - 1  # log2(val)
            else:
                rank = 0
            bb |= rank << (4 * (4 * r + c))
    return bb


def bitboard_to_board(bb):
    """
    Convert a C bitboard back to a 4x4 Python board.

    Args:
        bb: int — 64-bit bitboard

    Returns:
        List[List[int]] — 4x4 board with actual tile values
    """
    board = [[0] * 4 for _ in range(4)]
    for r in range(4):
        for c in range(4):
            rank = (bb >> (4 * (4 * r + c))) & 0xF
            board[r][c] = (1 << rank) if rank > 0 else 0
    return board


def find_best_move(board_2d):
    """
    Find the best move for the given board state.

    Args:
        board_2d: List[List[int]] — 4x4 board with actual tile values

    Returns:
        int — best direction (0=UP, 1=RIGHT, 2=DOWN, 3=LEFT), or -1 if game over
    """
    engine = _load_engine()
    bb = board_to_bitboard(board_2d)
    return engine.find_best_move(bb)


def execute_move_c(board_2d, direction):
    """
    Execute a move using the C engine (for testing/validation).

    Returns:
        List[List[int]] — resulting 4x4 board
    """
    engine = _load_engine()
    bb = board_to_bitboard(board_2d)
    new_bb = engine.execute_move(bb, direction)
    return bitboard_to_board(new_bb)


def play_game_c(seed):
    """
    Play a full game entirely in C for maximum speed.

    Args:
        seed: uint64 random seed

    Returns:
        dict with score, max_tile, moves, board (final 4x4 board)
    """
    engine = _load_engine()
    engine.play_game(seed)

    return {
        "score": engine.get_result_score(),
        "max_tile": engine.get_result_max_tile(),
        "moves": engine.get_result_moves(),
        "board": bitboard_to_board(engine.get_result_board()),
    }
