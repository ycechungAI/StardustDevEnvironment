"""The producer's numpy worker payback search against a literal port of Stardust's loop."""

import random

import numpy as np
from stardust.producer import producer


def _literal_payback_frame(minerals: list[int], start_frame: int, mineral_cost: int, time_to_build: int) -> int:
    f = start_frame
    for i in range(start_frame, len(minerals)):
        required = mineral_cost
        mining_time = i - f - time_to_build
        if mining_time > 0:
            required -= int(mining_time * producer.MINERALS_PER_WORKER_FRAME)
        if required <= 0:
            break
        if minerals[i] < required:
            f = i
    return f


def _random_minerals(rng: random.Random, frames: int) -> list[int]:
    """A forecast that climbs with income and dips where things are bought, with long stretches near the cost."""
    minerals = []
    value = rng.randrange(-100, 200)
    for _ in range(frames):
        roll = rng.random()
        if roll < 0.004:
            value -= rng.choice([50, 100, 150, 400])
        elif roll < 0.6:
            value += rng.choice([0, 1, 1, 2])
        minerals.append(value)
    return minerals


def test_worker_payback_frame_matches_stardust() -> None:
    rng = random.Random(11)
    saved = producer._predict_frames, producer._minerals
    try:
        for _ in range(400):
            frames = rng.choice([2000, 4500])
            minerals = _random_minerals(rng, frames)
            producer._predict_frames = frames
            producer._minerals = np.array(minerals, dtype=np.int64)

            start_frame = rng.randrange(0, frames)
            cost = rng.choice([50, 50, 75, 1])
            time_to_build = rng.choice([300, 20, 1])
            assert (producer._worker_payback_frame(start_frame, cost, time_to_build)
                    == _literal_payback_frame(minerals, start_frame, cost, time_to_build))
    finally:
        producer._predict_frames, producer._minerals = saved


def _literal_gas_deficit(gas: list[int], gas_cost: int, prerequisite_cost: int, start_frame: int,
                         reversed_prerequisites: list[tuple[int, int]]) -> tuple[int, int]:
    predict_frames = len(gas)
    gas_deficit_frame = predict_frames
    gas_deficit = 0
    gas_needed = prerequisite_cost + gas_cost
    prerequisite_index = 0
    for f in range(predict_frames - 1, -1, -1):
        deficit = gas_needed - gas[f]
        if deficit > 0:
            deficit_frame = f - ((deficit * 75) >> 4)
            if deficit_frame < gas_deficit_frame:
                gas_deficit_frame = deficit_frame
                gas_deficit = deficit
        if f == start_frame:
            gas_needed -= gas_cost
        while prerequisite_index < len(reversed_prerequisites) and f == reversed_prerequisites[prerequisite_index][0]:
            gas_needed -= reversed_prerequisites[prerequisite_index][1]
            prerequisite_index += 1
        if gas_needed == 0:
            break
    return gas_deficit_frame, gas_deficit


def test_gas_deficit_matches_stardust() -> None:
    rng = random.Random(17)
    saved = producer._predict_frames, producer._gas
    try:
        for _ in range(1000):
            frames = rng.choice([50, 2000, 4500])
            gas = _random_minerals(rng, frames)
            producer._predict_frames = frames
            producer._gas = np.array(gas, dtype=np.int64)

            # Prerequisites are usually in start frame order; sometimes not, or off the window
            starts = sorted(rng.randrange(-20, frames + 20) for _ in range(rng.randrange(1, 4)))
            if rng.random() < 0.2:
                rng.shuffle(starts)
            prerequisites = [(start, rng.choice([0, 25, 50, 100, 150])) for start in starts]
            prerequisite_cost = sum(price for _, price in prerequisites)
            start_frame = max(starts) + rng.randrange(-5, 300)
            gas_cost = rng.choice([0, 25, 50, 100])
            reversed_prerequisites = list(reversed(prerequisites))
            assert (producer._gas_deficit_with_prerequisites(gas_cost, prerequisite_cost, start_frame,
                                                             reversed_prerequisites)
                    == _literal_gas_deficit(gas, gas_cost, prerequisite_cost, start_frame, reversed_prerequisites))
    finally:
        producer._predict_frames, producer._gas = saved


def _literal_refinery_start_frame(minerals: list[int], desired: int, end: int, price: int, build_time: int,
                                  existing_start_frame: int | None) -> int:
    actual = desired
    for f in range(desired, end):
        if minerals[f] < price:
            actual = f + 1
            continue
        completion_frame = f + build_time
        needed = int(3.0 * (f - actual) * producer.MINERALS_PER_WORKER_FRAME)
        if existing_start_frame is None or completion_frame < existing_start_frame:
            needed += price
        if minerals[completion_frame] < needed:
            actual = f + 1
            continue
    return actual


def test_refinery_start_frame_matches_stardust() -> None:
    rng = random.Random(23)
    saved = producer._predict_frames, producer._minerals
    try:
        for _ in range(1000):
            frames = rng.choice([400, 2000, 4500])
            minerals = _random_minerals(rng, frames)
            producer._predict_frames = frames
            producer._minerals = np.array(minerals, dtype=np.int64)

            build_time = rng.choice([1, 50, 600])
            price = rng.choice([75, 100, 1])
            desired = rng.randrange(0, frames)
            existing = rng.choice([None, rng.randrange(0, frames + 50)])
            end = frames - build_time if existing is None else min(existing, frames - build_time)
            assert (producer._refinery_start_frame(desired, end, price, build_time, existing)
                    == _literal_refinery_start_frame(minerals, desired, end, price, build_time, existing))
    finally:
        producer._predict_frames, producer._minerals = saved
