"""Port of Workers/MiningOptimizationV2/DataModel/DeserializedPathCache.h: caches the most recently used deserialized
paths, so if paths come up often, we don't burn a lot of CPU time deserializing them.

Like Stardust, the cache can hold several copies of the same path if two workers need it at the same time, but only
one of them is indexed by key.
"""

from __future__ import annotations

from collections import OrderedDict
from dataclasses import dataclass
from itertools import count

from stardust.util.tile_position import TilePosition
from stardust.workers.mining_optimization_v2.data_model.path import ArrivalData, Path
from stardust.workers.mining_optimization_v2.data_model.position_and_velocity import PositionAndVelocity
from stardust.workers.mining_optimization_v2.data_model.serialized_path import SerializedPath

CAPACITY = 500

type DeserializedPathCacheKey = tuple[TilePosition, PositionAndVelocity]
type PathData[T: ArrivalData] = dict[TilePosition, dict[PositionAndVelocity, SerializedPath[T]]]


@dataclass
class Item[T: ArrivalData]:
    key: DeserializedPathCacheKey
    path: Path[T]


class DeserializedPathCache[T: ArrivalData]:
    def __init__(self, path_data: PathData[T]) -> None:
        self._path_data = path_data
        self._ids = count()

        # The cache, oldest first (Stardust's list, newest at the front)
        self._cache: OrderedDict[int, Item[T]] = OrderedDict()
        self._index: dict[DeserializedPathCacheKey, int] = {}

    def get(self, key: DeserializedPathCacheKey) -> Item[T] | None:
        # Try to find the serialized path
        patch_data = self._path_data.get(key[0])
        if patch_data is None:
            return None
        serialized_path = patch_data.get(key[1])
        if serialized_path is None:
            return None
        cannon_placement = serialized_path.active_cannon_placement()
        if cannon_placement is None:
            return None

        # Check if this key is already in the cache
        item_id = self._index.get(key)
        if item_id is not None and self._cache[item_id].path.cannon_placement == cannon_placement:
            # We got a cache hit: remove the path from the cache and return it
            item = self._cache.pop(item_id)
            del self._index[key]
            return Item(key, item.path)

        # Deserialize the path and return
        path = serialized_path.get(cannon_placement)
        if not path.next_positions:
            return None
        return Item(key, path)

    def put(self, item: Item[T]) -> None:
        # If the cache is at its capacity, remove the least-recently-added item
        if len(self._cache) == CAPACITY:
            _, oldest = self._cache.popitem(last=False)
            self._index.pop(oldest.key, None)

        item_id = next(self._ids)
        self._cache[item_id] = item
        self._index.setdefault(item.key, item_id)


class NullDeserializedPathCache[T: ArrivalData]:
    """A path cache that doesn't cache."""

    def __init__(self, path_data: PathData[T]) -> None:
        self._path_data = path_data

    def get(self, key: DeserializedPathCacheKey) -> Item[T] | None:
        # Try to find the serialized path
        patch_data = self._path_data.get(key[0])
        if patch_data is None:
            return None
        serialized_path = patch_data.get(key[1])
        if serialized_path is None:
            return None

        # Deserialize the path and return it
        return Item(key, serialized_path.get())

    def put(self, item: Item[T]) -> None:
        pass
