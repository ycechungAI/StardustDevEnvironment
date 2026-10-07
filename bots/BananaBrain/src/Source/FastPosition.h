struct FastPosition;
struct FastWalkPosition;
struct FastTilePosition;

struct FastPosition
{
	int x,y;
	
	FastPosition() : FastPosition(Positions::None) {}
	FastPosition(int x,int y) : x(x), y(y) {}
	FastPosition(const FastPosition&) = default;
	FastPosition(const FastWalkPosition& walk_position);
	FastPosition(const FastTilePosition& tile_position);
	FastPosition(const Position& position) : x(position.x), y(position.y) {}
	operator Position() const { return Position{x, y}; }
	bool isValid() const;
	FastPosition& makeValid();
	int getApproxDistance(const FastPosition& position) const;
	double getDistance(const FastPosition& position) const;
	double getLength() const;
	
	inline bool operator<(const FastPosition& o) const {
		return std::make_pair(x, y) < std::make_pair(o.x, o.y);
	}
	
	inline FastPosition operator+(const FastPosition& o) const {
		return FastPosition(x + o.x, y + o.y);
	}
	
	inline FastPosition operator-(const FastPosition& o) const {
		return FastPosition(x - o.x, y - o.y);
	}
	
	inline bool operator==(const FastPosition& o) const {
		return x == o.x && y == o.y;
	}
	
	inline bool operator!=(const FastPosition& o) const {
		return x != o.x || y != o.y;
	}
	
	inline FastPosition& operator+=(const FastPosition& o) {
		x += o.x;
		y += o.y;
		return *this;
	}
	
	inline FastPosition& operator/=(int d) {
		x /= d;
		y /= d;
		return *this;
	}
	
	inline FastPosition operator/(int d) {
		return FastPosition(x / d, y / d);
	}
};

struct FastWalkPosition
{
	int x,y;
	
	FastWalkPosition() : FastWalkPosition(WalkPositions::None) {}
	FastWalkPosition(int x,int y) : x(x), y(y) {}
	FastWalkPosition(const FastPosition& position);
	FastWalkPosition(const FastWalkPosition&) = default;
	FastWalkPosition(const FastTilePosition& tile_position);
	FastWalkPosition(const WalkPosition& walk_position) : x(walk_position.x), y(walk_position.y) {}
	operator WalkPosition() const { return WalkPosition{x, y}; }
	bool isValid() const;
	FastWalkPosition& makeValid();
	int getApproxDistance(const FastWalkPosition& walk_position) const;
	
	inline bool operator<(const FastWalkPosition& o) const {
		return std::make_pair(x, y) < std::make_pair(o.x, o.y);
	}
	
	inline FastWalkPosition operator+(const FastWalkPosition& o) const {
		return FastWalkPosition(x + o.x, y + o.y);
	}
	
	inline FastWalkPosition operator-(const FastWalkPosition& o) const {
		return FastWalkPosition(x - o.x, y - o.y);
	}
	
	inline bool operator==(const FastWalkPosition& o) const {
		return x == o.x && y == o.y;
	}
	
	inline bool operator!=(const FastWalkPosition& o) const {
		return x != o.x || y != o.y;
	}
	
	inline FastWalkPosition& operator+=(const FastWalkPosition& o) {
		x += o.x;
		y += o.y;
		return *this;
	}
	
	inline FastWalkPosition& operator/=(int d) {
		x /= d;
		y /= d;
		return *this;
	}
	
	inline FastWalkPosition operator/(int d) {
		return FastWalkPosition(x / d, y / d);
	}
};

struct FastTilePosition
{
	int x,y;
	
	FastTilePosition() : FastTilePosition(TilePositions::None) {}
	FastTilePosition(int x,int y) : x(x), y(y) {}
	FastTilePosition(const FastPosition& position);
	FastTilePosition(const FastWalkPosition& walk_position);
	FastTilePosition(const FastTilePosition&) = default;
	FastTilePosition(const TilePosition& tile_position) : x(tile_position.x), y(tile_position.y) {}
	operator TilePosition() const { return TilePosition{x, y}; }
	bool isValid() const;
	FastTilePosition& makeValid();
	int getApproxDistance(const FastTilePosition& tile_position) const;
	
	inline bool operator<(const FastTilePosition& o) const {
		return std::make_pair(x, y) < std::make_pair(o.x, o.y);
	}
	
	inline FastTilePosition operator+(const FastTilePosition& o) const {
		return FastTilePosition(x + o.x, y + o.y);
	}
	
	inline FastTilePosition operator-(const FastTilePosition& o) const {
		return FastTilePosition(x - o.x, y - o.y);
	}
	
	inline bool operator==(const FastTilePosition& o) const {
		return x == o.x && y == o.y;
	}
	
	inline bool operator!=(const FastTilePosition& o) const {
		return x != o.x || y != o.y;
	}
	
	inline FastTilePosition& operator+=(const FastTilePosition& o) {
		x += o.x;
		y += o.y;
		return *this;
	}
	
	inline FastTilePosition& operator/=(int d) {
		x /= d;
		y /= d;
		return *this;
	}
	
	inline FastTilePosition operator/(int d) {
		return FastTilePosition(x / d, y / d);
	}
};

namespace std {
	template<>
	struct hash<FastPosition>
	{
		size_t operator()(const FastPosition& key) const
		{
			return (std::hash<int>()(key.y) << 1) ^ std::hash<int>()(key.x);
		}
	};
	
	template<>
	struct hash<FastWalkPosition>
	{
		size_t operator()(const FastWalkPosition& key) const
		{
			return (std::hash<int>()(key.y) << 1) ^ std::hash<int>()(key.x);
		}
	};
	
	template<>
	struct hash<FastTilePosition>
	{
		size_t operator()(const FastTilePosition& key) const
		{
			return (std::hash<int>()(key.y) << 1) ^ std::hash<int>()(key.x);
		}
	};
}
