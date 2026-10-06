"""The producer's vectorized resource scans, checked against literal transcriptions of Stardust's loops."""

import random

import numpy as np
import pytest

from bwapi import UnitTypes
from stardust.cpp import to_int
from stardust.producer import producer
from stardust.workers.worker_gather_optimizer import MINERALS_PER_WORKER_FRAME

P = 300


@pytest.fixture
def timelines(monkeypatch: pytest.MonkeyPatch) -> random.Random:
    rng = random.Random(1234)
    monkeypatch.setattr(producer, "_predict_frames", P)
    monkeypatch.setattr(producer, "_minerals", np.zeros(P, dtype=np.int64))
    monkeypatch.setattr(producer, "_gas", np.zeros(P, dtype=np.int64))
    monkeypatch.setattr(producer, "_supply", np.zeros(P, dtype=np.int64))
    monkeypatch.setattr(producer, "_total_supply", np.zeros(P, dtype=np.int64))
    return rng


def randomize(rng: random.Random, array: np.ndarray, low: int, high: int) -> None:
    # Mostly increasing resource curves with dips, like real income minus spending
    value = rng.randint(low, high)
    for f in range(len(array)):
        value += rng.randint(-15, 20)
        array[f] = value


def test_scan_back(timelines: random.Random) -> None:
    rng = timelines
    for _ in range(300):
        randomize(rng, producer._minerals, -100, 200)
        amount = rng.randint(0, 300)
        high = rng.randint(-5, P - 1)
        low = rng.randint(0, P + 5)

        f = high
        while f >= low:
            if producer._minerals[f] < amount:
                break
            f -= 1
        assert producer._scan_back(producer._minerals, amount, high, low) == f + 1


def test_first_failing_frame(timelines: random.Random) -> None:
    rng = timelines
    for _ in range(300):
        randomize(rng, producer._gas, -50, 300)
        stops = [(rng.randint(-20, 120), rng.randint(0, 400)) for _ in range(rng.randint(1, 4))]
        low = rng.randint(0, P - 1)

        expected_last = None
        for f in range(P - 1, low - 1, -1):
            if any((f - offset < P) and (f - offset < 0 or producer._gas[f - offset] < needed)
                   for offset, needed in stops):
                expected_last = f
                break
        expected_first = None
        for f in range(low, P):
            if any((f - offset < P) and (f - offset < 0 or producer._gas[f - offset] < needed)
                   for offset, needed in stops):
                expected_first = f
                break

        assert producer._first_failing_frame(producer._gas, stops, low, P - 1, last=True) == expected_last
        assert producer._first_failing_frame(producer._gas, stops, low, P - 1, last=False) == expected_first


def test_update_resource_collection(timelines: random.Random) -> None:
    rng = timelines
    for _ in range(50):
        randomize(rng, producer._minerals, 0, 100)
        expected = producer._minerals.copy()
        from_frame = rng.randint(0, P + 2)
        change = rng.choice([-3, -1, 1, 3])
        delta = change * MINERALS_PER_WORKER_FRAME
        for f in range(from_frame + 1, P):
            expected[f] += to_int(delta * (f - from_frame))

        producer._update_resource_collection(producer._minerals, from_frame, change, MINERALS_PER_WORKER_FRAME)
        assert np.array_equal(producer._minerals, expected)


def test_add_provided_supply(timelines: random.Random) -> None:
    rng = timelines
    for _ in range(100):
        total = rng.randint(300, 420)
        producer._total_supply[:] = total
        producer._supply[:] = rng.randint(0, 20)
        expected_total = producer._total_supply.copy()
        expected_supply = producer._supply.copy()
        amount = 16
        from_frame = rng.randint(0, P - 1)

        expected_total[from_frame:] += amount
        if expected_total[P - 1] <= 400:
            expected_supply[from_frame:] += amount
        else:
            for f in range(from_frame, P):
                if expected_total[f] >= 400 + amount:
                    break
                expected_supply[f] += 400 + amount - expected_total[f]

        producer._add_provided_supply(amount, from_frame)
        assert np.array_equal(producer._total_supply, expected_total)
        assert np.array_equal(producer._supply, expected_supply)


def test_mineral_block_frame(timelines: random.Random) -> None:
    rng = timelines
    for _ in range(200):
        randomize(rng, producer._minerals, -50, 50)
        reassign_frame = rng.randint(0, P + 2)
        f = P - 1
        while f > reassign_frame:
            if producer._minerals[f] < to_int((f - reassign_frame) * MINERALS_PER_WORKER_FRAME):
                break
            f -= 1
        assert producer._mineral_block_frame(reassign_frame) == f


def test_gas_deficit(timelines: random.Random) -> None:
    rng = timelines
    for _ in range(200):
        randomize(rng, producer._gas, -100, 100)
        gas_cost = rng.randint(25, 200)
        start_frame = rng.randint(0, P + 2)
        deficit_frame, deficit = P, 0
        for f in range(P - 1, start_frame - 1, -1):
            d = gas_cost - int(producer._gas[f])
            if d > 0:
                frame = f - ((d * 75) >> 4)
                if frame < deficit_frame:
                    deficit_frame, deficit = frame, d
        assert producer._gas_deficit(gas_cost, start_frame) == (deficit_frame, deficit)


def test_supply_block_frames(timelines: random.Random) -> None:
    rng = timelines
    for _ in range(200):
        for f in range(P):
            producer._supply[f] = rng.choice([0, 1, 2, 4, 8])
            producer._total_supply[f] = rng.choice([200] * 30 + [400])
        required = rng.choice([2, 4])
        start_frame = rng.randint(0, P - 1)

        expected: list[int] | None = []
        blocked = False
        for f in range(start_frame, P):
            if producer._supply[f] < required:
                if producer._total_supply[f] >= 400:
                    expected = None
                    break
                if not blocked:
                    assert expected is not None
                    expected.append(f)
                    blocked = True
            else:
                blocked = False
        assert producer._supply_block_frames(start_frame, required) == expected


def test_probe_mineral_shift(timelines: random.Random) -> None:
    rng = timelines
    probe = UnitTypes.Protoss_Probe
    for _ in range(100):
        randomize(rng, producer._minerals, -60, 60)
        item = producer._ProductionItem(probe, rng.randint(0, P // 2))
        start = item.start_frame
        cost = item.mineral_price()

        # Stardust's loop, after frameWhenResourcesMet found a later frame
        f = producer._scan_back(producer._minerals, cost, P - 1, start)
        if f > start:
            f = start
            build = producer._build_time(probe)
            for i in range(start, P):
                required = cost
                mining_time = i - f - build
                if mining_time > 0:
                    required -= to_int(mining_time * MINERALS_PER_WORKER_FRAME)
                if required <= 0:
                    break
                if producer._minerals[i] < required:
                    f = i

        result = producer._shift_for_minerals(item, producer._ItemSet())
        if f == P:
            assert not result
        else:
            assert result
            assert item.start_frame == max(start, f)


def test_item_set_ordering() -> None:
    items = producer._ItemSet()
    probe = UnitTypes.Protoss_Probe
    first = producer._ProductionItem(probe, 10)
    second = producer._ProductionItem(probe, 5)
    third = producer._ProductionItem(probe, 10)
    for item in (first, second, third):
        items.insert(item)
    assert items.items == [second, first, third]

    producer._shift_all(items, 1, 3)
    assert [item.start_frame for item in items.items] == [5, 13, 13]
    assert items.remove(first) == 1
    assert items.items == [second, third]
