"""
2048 Game Web Server

Provides a REST API for the web frontend and serves static files.
"""

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pydantic import BaseModel
from typing import Optional
import random

from game.core import (
    GameState,
    Direction,
    new_game,
    step,
    get_available_moves,
    is_game_over,
    BOARD_SIZE,
)
from agent.ai import find_best_move

app = FastAPI(title="2048 Game")

# In-memory game sessions (simple dict; for single-user is fine)
games: dict[str, GameState] = {}
rngs: dict[str, random.Random] = {}


class MoveRequest(BaseModel):
    direction: int  # 0=UP, 1=RIGHT, 2=DOWN, 3=LEFT


class NewGameRequest(BaseModel):
    seed: Optional[int] = None


@app.post("/api/new")
def api_new_game(req: NewGameRequest = NewGameRequest()):
    """Start a new game. Optionally provide a seed for reproducibility."""
    rng = random.Random(req.seed)
    state = new_game(rng)
    session_id = "default"
    games[session_id] = state
    rngs[session_id] = rng
    return state.to_dict()


@app.post("/api/move")
def api_move(req: MoveRequest):
    """Make a move. direction: 0=UP, 1=RIGHT, 2=DOWN, 3=LEFT"""
    session_id = "default"
    if session_id not in games:
        state = new_game()
        games[session_id] = state
        rngs[session_id] = random.Random()

    state = games[session_id]
    rng = rngs[session_id]

    direction = Direction(req.direction)
    new_state, reward, done = step(state, direction, rng)
    games[session_id] = new_state

    result = new_state.to_dict()
    result["reward"] = reward
    result["moved"] = new_state.board != state.board
    return result


@app.get("/api/state")
def api_state():
    """Get current game state."""
    session_id = "default"
    if session_id not in games:
        return api_new_game()
    return games[session_id].to_dict()


@app.post("/api/agent/move")
def api_agent_move():
    """Let the AI agent choose and execute the best move."""
    session_id = "default"
    if session_id not in games:
        state = new_game()
        games[session_id] = state
        rngs[session_id] = random.Random()

    state = games[session_id]
    rng = rngs[session_id]

    direction = find_best_move(state.board)
    if direction < 0:
        result = state.to_dict()
        result["moved"] = False
        result["direction"] = -1
        result["reward"] = 0
        return result

    new_state, reward, done = step(state, Direction(direction), rng)
    games[session_id] = new_state

    result = new_state.to_dict()
    result["reward"] = reward
    result["moved"] = new_state.board != state.board
    result["direction"] = direction
    return result


# Serve static files
app.mount("/static", StaticFiles(directory="static"), name="static")


@app.get("/")
def index():
    return FileResponse("static/index.html")
