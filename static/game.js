/**
 * 2048 Game Frontend
 *
 * Communicates with the FastAPI backend via REST API.
 * Supports keyboard (arrow keys) and touch/swipe input.
 */

const API = {
    async newGame() {
        const res = await fetch('/api/new', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}' });
        return res.json();
    },
    async move(direction) {
        const res = await fetch('/api/move', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ direction }),
        });
        return res.json();
    },
    async getState() {
        const res = await fetch('/api/state');
        return res.json();
    },
};

// Direction constants (must match server)
const DIR = { UP: 0, RIGHT: 1, DOWN: 2, LEFT: 3 };

// DOM elements
const boardEl = document.getElementById('board');
const scoreEl = document.getElementById('score');
const bestEl = document.getElementById('best');
const overlayEl = document.getElementById('game-overlay');
const newGameBtn = document.getElementById('new-game-btn');
const retryBtn = document.getElementById('retry-btn');

let currentBoard = null;
let bestScore = parseInt(localStorage.getItem('best2048') || '0', 10);
let isAnimating = false;

bestEl.textContent = bestScore;

// ---- Rendering ----

function renderBoard(board, previousBoard) {
    boardEl.innerHTML = '';
    for (let r = 0; r < 4; r++) {
        for (let c = 0; c < 4; c++) {
            const val = board[r][c];
            const cell = document.createElement('div');
            cell.className = 'cell';
            cell.dataset.value = val <= 2048 ? val : 'super';
            if (val > 2048) cell.classList.add('super');
            cell.textContent = val === 0 ? '' : val;

            // Detect new tiles for animation
            if (previousBoard && val !== 0 && previousBoard[r][c] === 0) {
                cell.classList.add('new-tile');
            }

            boardEl.appendChild(cell);
        }
    }
}

function updateScore(score) {
    const prev = parseInt(scoreEl.textContent, 10);
    scoreEl.textContent = score;

    if (score > bestScore) {
        bestScore = score;
        bestEl.textContent = bestScore;
        localStorage.setItem('best2048', bestScore.toString());
    }
}

function showGameOver() {
    overlayEl.classList.add('active');
}

function hideGameOver() {
    overlayEl.classList.remove('active');
}

// ---- Game Actions ----

async function startNewGame() {
    hideGameOver();
    const state = await API.newGame();
    currentBoard = state.board;
    renderBoard(state.board, null);
    updateScore(state.score);
}

async function makeMove(direction) {
    if (isAnimating) return;
    isAnimating = true;

    try {
        const previousBoard = currentBoard;
        const state = await API.move(direction);

        if (!state.moved) {
            isAnimating = false;
            return;
        }

        currentBoard = state.board;
        renderBoard(state.board, previousBoard);
        updateScore(state.score);

        if (state.game_over) {
            setTimeout(showGameOver, 300);
        }
    } finally {
        isAnimating = false;
    }
}

// ---- Keyboard Input ----

document.addEventListener('keydown', (e) => {
    const keyMap = {
        ArrowUp: DIR.UP,
        ArrowRight: DIR.RIGHT,
        ArrowDown: DIR.DOWN,
        ArrowLeft: DIR.LEFT,
    };

    if (e.key in keyMap) {
        e.preventDefault();
        makeMove(keyMap[e.key]);
    }
});

// ---- Touch / Swipe Input ----

let touchStartX = 0;
let touchStartY = 0;

boardEl.addEventListener('touchstart', (e) => {
    touchStartX = e.touches[0].clientX;
    touchStartY = e.touches[0].clientY;
}, { passive: true });

boardEl.addEventListener('touchend', (e) => {
    const dx = e.changedTouches[0].clientX - touchStartX;
    const dy = e.changedTouches[0].clientY - touchStartY;
    const absDx = Math.abs(dx);
    const absDy = Math.abs(dy);
    const minSwipe = 30;

    if (Math.max(absDx, absDy) < minSwipe) return;

    if (absDx > absDy) {
        makeMove(dx > 0 ? DIR.RIGHT : DIR.LEFT);
    } else {
        makeMove(dy > 0 ? DIR.DOWN : DIR.UP);
    }
}, { passive: true });

// ---- Button Events ----

newGameBtn.addEventListener('click', startNewGame);
retryBtn.addEventListener('click', startNewGame);

// ---- Init ----

startNewGame();
