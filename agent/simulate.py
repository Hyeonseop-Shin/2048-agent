"""
2048 AI Simulation Runner

Runs multiple games using the Expectimax agent (C engine) and reports statistics.

Usage:
    uv run python -m agent.simulate [--games N] [--seed S]
"""

import argparse
import time
from collections import Counter

from agent.ai import play_game_c


def run_simulation(num_games, base_seed=None):
    """Run multiple games and print aggregate statistics."""
    results = []

    print(f"Running {num_games} game(s)...\n")

    for i in range(num_games):
        seed = (
            (base_seed + i) if base_seed is not None else (int(time.time() * 1000) + i)
        )

        start = time.time()
        stats = play_game_c(seed)
        elapsed = time.time() - start
        stats["time"] = elapsed

        results.append(stats)

        print(
            f"  Game {i + 1:>3d}: score={stats['score']:>7d}  "
            f"max_tile={stats['max_tile']:>5d}  "
            f"moves={stats['moves']:>5d}  "
            f"time={elapsed:.1f}s"
        )

    # ---- Aggregate statistics ----
    print("\n" + "=" * 60)
    print("SIMULATION RESULTS")
    print("=" * 60)

    scores = [r["score"] for r in results]
    max_tiles = [r["max_tile"] for r in results]
    move_counts = [r["moves"] for r in results]
    times = [r["time"] for r in results]

    scores_sorted = sorted(scores)
    n = len(scores_sorted)
    median = (
        scores_sorted[n // 2]
        if n % 2 == 1
        else (scores_sorted[n // 2 - 1] + scores_sorted[n // 2]) / 2
    )

    print(f"\nGames played:   {num_games}")
    print(f"\nScore:")
    print(f"  Mean:         {sum(scores) / n:,.0f}")
    print(f"  Median:       {median:,.0f}")
    print(f"  Min:          {min(scores):,d}")
    print(f"  Max:          {max(scores):,d}")

    print(f"\nMoves:")
    print(f"  Mean:         {sum(move_counts) / n:,.0f}")
    print(f"  Total:        {sum(move_counts):,d}")

    print(f"\nTime:")
    print(f"  Total:        {sum(times):,.1f}s")
    print(f"  Per game:     {sum(times) / n:,.1f}s")
    total_time = sum(times)
    if total_time > 0:
        print(f"  Moves/sec:    {sum(move_counts) / total_time:,.1f}")

    # Tile achievement rates
    tile_counts = Counter(max_tiles)

    print(f"\nTile achievement rates:")
    for tile in [32768, 16384, 8192, 4096, 2048, 1024, 512, 256]:
        achieved = sum(1 for t in max_tiles if t >= tile)
        rate = achieved / n * 100
        if achieved > 0:
            print(f"  {tile:>5d}: {achieved:>3d}/{n}  ({rate:5.1f}%)")

    print(f"\nMax tile distribution:")
    for tile in sorted(tile_counts.keys(), reverse=True):
        count = tile_counts[tile]
        bar = "#" * (count * 40 // n) if n > 0 else ""
        print(f"  {tile:>5d}: {count:>3d}  {bar}")

    print("=" * 60)


def main():
    parser = argparse.ArgumentParser(description="2048 AI Simulation")
    parser.add_argument(
        "--games",
        "-n",
        type=int,
        default=10,
        help="Number of games to play (default: 10)",
    )
    parser.add_argument(
        "--seed",
        "-s",
        type=int,
        default=None,
        help="Base random seed (default: time-based)",
    )
    args = parser.parse_args()

    run_simulation(args.games, args.seed)


if __name__ == "__main__":
    main()
