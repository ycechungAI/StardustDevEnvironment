"""Port of Map/PathFinding/NavigationGrid.{h,cpp}: a flow field towards a goal at tile resolution.

Each tile stores its path cost to the goal and the next tile on the path; tiles also remember which neighbours point
at them (a bitmask) so blocking a tile can invalidate exactly the paths through it. Updates are incremental.

Data is kept in flat arrays indexed by x + y * map_width; GridNode is a lightweight view of one cell.

Equal-cost ties in the priority queue are broken first-in-first-out, where C++ std::priority_queue order is
unspecified, so equal-cost paths may point in different (equally short) directions than in Stardust.
"""

from __future__ import annotations

import heapq
from itertools import count

from bwapi import Position, TilePosition, TilePositions, WalkPosition
from stardust.cpp import USHRT_MAX
from stardust.instrumentation import cherryvis

COST_STRAIGHT = 32
COST_DIAGONAL = 45

_map_width = 0
_map_height = 0


def initialize_globals(map_width: int, map_height: int) -> None:
    """NavigationGridGlobals::initialize."""
    global _map_width, _map_height
    _map_width = map_width
    _map_height = map_height


class GridNode:
    """A view of one cell of a navigation grid."""

    __slots__ = ("_grid", "index", "x", "y")

    def __init__(self, grid: NavigationGrid, index: int) -> None:
        self._grid = grid
        self.index = index
        self.x = index % _map_width
        self.y = index // _map_width

    @property
    def cost(self) -> int:
        return self._grid._cost[self.index]

    @property
    def next_node(self) -> GridNode | None:
        next_index = self._grid._next[self.index]
        return GridNode(self._grid, next_index) if next_index >= 0 else None

    @property
    def prev_nodes(self) -> int:
        return self._grid._prev[self.index]

    @property
    def tile(self) -> TilePosition:
        return TilePosition(self.x, self.y)

    def center(self) -> Position:
        return Position((self.x << 5) + 16, (self.y << 5) + 16)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, GridNode) and other._grid is self._grid and other.index == self.index

    def __hash__(self) -> int:
        return hash((id(self._grid), self.index))

    def __str__(self) -> str:
        text = f"({self.x},{self.y}:{self.cost})"
        next_node = self.next_node
        if next_node is not None:
            text += f"->({next_node.x},{next_node.y}:{next_node.cost})"
        return text


class NavigationGrid:
    def __init__(self, goal_center: TilePosition, goal_top_left: TilePosition = TilePositions.Invalid,
                 goal_size: TilePosition = TilePositions.Invalid) -> None:
        self.goal = goal_center
        size = _map_width * _map_height
        self._cost = [USHRT_MAX] * size
        self._next = [-1] * size
        self._prev = [0] * size  # Bit `direction` set when the neighbour in that direction has this node as next
        self._queue: list[tuple[int, int, int, bool]] = []  # (priority, sequence, index, diagonal)
        self._sequence = count()
        self._pending_blocking_tiles: set[TilePosition] = set()

        def push_initial_tile(tile: TilePosition) -> None:
            if not tile.isValid():
                return
            index = tile.x + tile.y * _map_width
            self._cost[index] = 0
            self._push(COST_STRAIGHT, index, False)
            self._push(COST_DIAGONAL, index, True)

        if goal_top_left.isValid() and goal_size.isValid():
            for x in range(-1, goal_size.x + 1):
                push_initial_tile(goal_top_left + TilePosition(x, -1))
                push_initial_tile(goal_top_left + TilePosition(x, goal_size.y))
            for y in range(goal_size.y):
                push_initial_tile(goal_top_left + TilePosition(-1, y))
                push_initial_tile(goal_top_left + TilePosition(goal_size.x, y))
        else:
            push_initial_tile(goal_center)

        self.update()

    def _push(self, priority: int, index: int, diagonal: bool) -> None:
        heapq.heappush(self._queue, (priority, next(self._sequence), index, diagonal))

    # Access by position (the C++ operator[] overloads)

    def node(self, pos: Position | WalkPosition | TilePosition) -> GridNode:
        return GridNode(self, self.index_of(pos))

    @staticmethod
    def index_of(pos: Position | WalkPosition | TilePosition) -> int:
        if isinstance(pos, TilePosition):
            return pos.x + pos.y * _map_width
        if isinstance(pos, WalkPosition):
            return (pos.x >> 2) + (pos.y >> 2) * _map_width
        return (pos.x >> 5) + (pos.y >> 5) * _map_width

    def cost_at(self, pos: Position | WalkPosition | TilePosition) -> int:
        """Path cost from the position to the goal; USHRT_MAX if unreachable or off the map."""
        if not pos.isValid():
            return USHRT_MAX
        return self._cost[self.index_of(pos)]

    def update(self) -> None:
        from stardust.map import game_map

        self._update_blocking_tiles()
        if not self._queue:
            return

        width, height = _map_width, _map_height
        cost = self._cost
        next_node = self._next
        prev = self._prev
        queue = self._queue
        sequence = self._sequence
        walkable = game_map.walkability_grid()
        mineral_line = game_map.own_mineral_line_grid()
        allow_diagonal_override = game_map.map_specific_override().allow_diagonal_pathing_through

        def allow_diagonal_connection_through(x: int, y: int) -> bool:
            return bool(walkable[x + y * width]) or allow_diagonal_override(x, y)

        def visit(current: int, cx: int, cy: int, x: int, y: int, direction: int) -> None:
            if x < 0 or x >= width or y < 0 or y >= height:
                return
            index = x + y * width

            # Compute the cost of this node; if it already has a lower cost, we don't need to consider it
            diagonal = direction & 1
            new_cost = cost[current] + (COST_DIAGONAL if diagonal else COST_STRAIGHT)
            if cost[index] <= new_cost:
                return

            # Don't allow a diagonal connection from a walkable tile through a blocked tile
            if diagonal and walkable[index] and (not allow_diagonal_connection_through(x, cy)
                                                 or not allow_diagonal_connection_through(cx, y)):
                return

            # Make the connection if it isn't already done
            cost[index] = new_cost
            old_next = next_node[index]
            if old_next != current:
                # Remove the reverse connection if the node was previously connected to another one
                if old_next >= 0:
                    ox = old_next % width
                    oy = old_next // width
                    if ox < x:
                        bit = 3 if oy < y else (7 if oy > y else 2)
                    elif ox > x:
                        bit = 1 if oy < y else (5 if oy > y else 0)
                    else:
                        bit = 6 if oy < y else 4
                    prev[old_next] &= ~(1 << bit)

                # Create the connection
                next_node[index] = current
                prev[current] |= 1 << direction

            # Queue the node if it is walkable
            if walkable[index] and not mineral_line[index]:
                heapq.heappush(queue, (new_cost + COST_STRAIGHT, next(sequence), index, False))
                heapq.heappush(queue, (new_cost + COST_DIAGONAL, next(sequence), index, True))

        while queue:
            _, _, index, diagonal = heapq.heappop(queue)
            if not walkable[index] or mineral_line[index] or cost[index] == USHRT_MAX:
                continue

            x = index % width
            y = index // width
            if diagonal:
                visit(index, x, y, x - 1, y + 1, 1)
                visit(index, x, y, x + 1, y + 1, 3)
                visit(index, x, y, x - 1, y - 1, 5)
                visit(index, x, y, x + 1, y - 1, 7)
            else:
                visit(index, x, y, x - 1, y, 0)
                visit(index, x, y, x + 1, y, 2)
                visit(index, x, y, x, y - 1, 4)
                visit(index, x, y, x, y + 1, 6)

    def add_blocking_object(self, tile: TilePosition, size: TilePosition) -> None:
        for x in range(tile.x, tile.x + size.x):
            for y in range(tile.y, tile.y + size.y):
                self._pending_blocking_tiles.add(TilePosition(x, y))

        # Add tiles that have diagonal connections through a corner of the object
        def add_corner_tile(to_x: int, to_y: int, direction: int) -> None:
            if to_x < 0 or to_x >= _map_width or to_y < 0 or to_y >= _map_height:
                return
            if self._prev[to_x + to_y * _map_width] & (1 << direction):
                self._pending_blocking_tiles.add(TilePosition(to_x, to_y))

        add_corner_tile(tile.x, tile.y - 1, 1)  # up-right, top-left corner
        add_corner_tile(tile.x + size.x, tile.y + size.y - 1, 1)  # up-right, bottom-right corner
        add_corner_tile(tile.x + size.x - 1, tile.y - 1, 3)  # up-left, top-right corner
        add_corner_tile(tile.x - 1, tile.y + size.y - 1, 3)  # up-left, bottom-left corner
        add_corner_tile(tile.x + size.x, tile.y, 5)  # down-right, top-right corner
        add_corner_tile(tile.x, tile.y + size.y, 5)  # down-right, bottom-left corner
        add_corner_tile(tile.x - 1, tile.y, 7)  # down-left, top-left corner
        add_corner_tile(tile.x + size.x - 1, tile.y + size.y, 7)  # down-left, bottom-right corner

    def add_blocking_tiles(self, tiles: set[TilePosition]) -> None:
        self._pending_blocking_tiles.update(tiles)

    def remove_blocking_object(self, tile: TilePosition, size: TilePosition) -> None:
        self.remove_blocking_tiles({TilePosition(x, y)
                                    for x in range(tile.x, tile.x + size.x)
                                    for y in range(tile.y, tile.y + size.y)})

    def _bordering(self, x: int, y: int, bordering: set[int]) -> None:
        for bx, by in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1),
                       (x - 1, y - 1), (x + 1, y - 1), (x - 1, y + 1), (x + 1, y + 1)):
            if 0 <= bx < _map_width and 0 <= by < _map_height:
                bordering.add(bx + by * _map_width)

    def _enqueue_bordering(self, bordering: set[int]) -> None:
        # C++ iterates a std::set of node pointers, i.e. in index order
        for index in sorted(bordering):
            if self._next[index] >= 0:
                self._push(self._cost[index] + COST_STRAIGHT, index, False)
                self._push(self._cost[index] + COST_DIAGONAL, index, True)

    def remove_blocking_tiles(self, tiles: set[TilePosition]) -> None:
        """Reset the nodes of the removed blocking tiles, then queue their valid neighbours to re-path them."""
        bordering: set[int] = set()
        for tile in tiles:
            index = tile.x + tile.y * _map_width
            self._cost[index] = USHRT_MAX
            self._next[index] = -1
            self._bordering(tile.x, tile.y, bordering)
        self._enqueue_bordering(bordering)

    def _update_blocking_tiles(self) -> None:
        """Invalidate every path through the pending blocking tiles, then queue the still-valid tiles bordering an
        invalidated tile; the next update gives the invalidated tiles new paths from these."""
        if not self._pending_blocking_tiles:
            return

        width = _map_width
        cost, next_node, prev = self._cost, self._next, self._prev
        queue = [tile.x + tile.y * width for tile in sorted(self._pending_blocking_tiles)]
        bordering: set[int] = set()

        def visit(x: int, y: int) -> None:
            index = x + y * width
            cost[index] = USHRT_MAX
            next_node[index] = -1
            if prev[index]:
                queue.append(index)
            self._bordering(x, y, bordering)

        position = 0
        while position < len(queue):
            current = queue[position]
            position += 1
            x = current % width
            y = current // width
            bits = prev[current]
            if bits & (1 << 1):
                visit(x - 1, y + 1)
            if bits & (1 << 3):
                visit(x + 1, y + 1)
            if bits & (1 << 5):
                visit(x - 1, y - 1)
            if bits & (1 << 7):
                visit(x + 1, y - 1)
            if bits & (1 << 0):
                visit(x - 1, y)
            if bits & (1 << 2):
                visit(x + 1, y)
            if bits & (1 << 4):
                visit(x, y - 1)
            if bits & (1 << 6):
                visit(x, y + 1)
            prev[current] = 0

        self._enqueue_bordering(bordering)
        self._pending_blocking_tiles.clear()

    def dump_heatmap(self) -> None:
        costs = [0 if c == USHRT_MAX else c for c in self._cost]
        cherryvis.add_heatmap(f"Navigation@{self.goal}", costs, _map_width, _map_height)
