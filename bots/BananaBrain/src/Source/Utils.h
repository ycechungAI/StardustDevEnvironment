#pragma once

template <class T>
class Singleton
{
public:
	static T& Instance()
	{
		static T instance;
		return instance;
	}
	
protected:
	Singleton() {}
	virtual ~Singleton() {}
	
private:
	Singleton(const Singleton&) = delete;
	Singleton(Singleton&&) = delete;
	Singleton& operator=(const Singleton&) = delete;
};

template <typename T>
bool common_elements(const std::set<T>& a,const std::set<T>& b)
{
	for (T x : a) if (b.count(x) > 0) return true;
	return false;
}

template <typename K,typename V>
using key_value_vector = std::vector<std::pair<K,V>>;

template <typename K,typename V>
const K& key_with_smallest_value(const std::map<K,V>& map,const K& default_key = K())
{
	if (map.empty()) {
		return default_key;
	} else {
		auto it = std::min_element(map.cbegin(), map.cend(), [](auto& entry1,auto& entry2) {
			return entry1.second < entry2.second;
		});
		return (*it).first;
	}
}

template <typename K,typename V>
const K& key_with_smallest_value(const key_value_vector<K,V>& vector,const K& default_key = K())
{
	if (vector.empty()) {
		return default_key;
	} else {
		auto it = std::min_element(vector.cbegin(), vector.cend(), [](auto& entry1,auto& entry2) {
			return entry1.second < entry2.second;
		});
		return (*it).first;
	}
}

template <typename K,typename V>
const K& key_with_largest_value(const std::map<K,V>& map,const K& default_key = K())
{
	if (map.empty()) {
		return default_key;
	} else {
		auto it = std::min_element(map.cbegin(), map.cend(), [](auto& entry1,auto& entry2) {
			return entry1.second > entry2.second;
		});
		return (*it).first;
	}
}

template <typename K,typename V>
const K& key_with_largest_value(const key_value_vector<K,V>& vector,const K& default_key = K())
{
	if (vector.empty()) {
		return default_key;
	} else {
		auto it = std::min_element(vector.cbegin(), vector.cend(), [](auto& entry1,auto& entry2) {
			return entry1.second > entry2.second;
		});
		return (*it).first;
	}
}

template <typename K,typename V>
std::vector<K> keys_sorted(const std::map<K,V>& map)
{
	std::vector<K> result;
	result.reserve(map.size());
	for (auto& entry : map) result.push_back(entry.first);
	std::sort(result.begin(), result.end(), [&map](auto& key1,auto& key2) {
		return map.at(key1) < map.at(key2);
	});
	return result;
}

template <typename V,typename Func>
const V smallest_priority(const std::vector<V>& vector,Func&& priority_function,const V& default_value = V())
{
	auto it = std::min_element(vector.begin(), vector.end(), [&priority_function](const V& a,const V& b){
		return priority_function(a) < priority_function(b);
	});
	return (it == vector.end()) ? default_value : *it;
}

template <typename V,typename Func>
const V smallest_priority(const std::set<V>& set,Func&& priority_function,const V& default_value = V())
{
	auto it = std::min_element(set.begin(), set.end(), [&priority_function](const V& a,const V& b){
		return priority_function(a) < priority_function(b);
	});
	return (it == set.end()) ? default_value : *it;
}

template <typename K,typename V>
const V get_or_default(const std::map<K,V>& map,const K& key,const V& default_value = V())
{
	auto it = map.find(key);
	return (it == map.end()) ? default_value : it->second;
}

template <typename T,typename UnaryPredicate>
void remove_elements_in_place(std::vector<T>& vector,const UnaryPredicate& pred)
{
	vector.erase(std::remove_if(vector.begin(), vector.end(), pred), vector.end());
}

template <typename T>
void remove_duplicates(std::vector<T>& vector)
{
	std::sort(vector.begin(), vector.end());
	vector.erase(std::unique(vector.begin(), vector.end()), vector.end());
}

template <typename T>
bool contains(const std::set<T>& set, const T& value);

template <typename K,typename V>
void remove_missing_keys(std::map<K,V>& map,const std::vector<K>& keep)
{
	std::set<K> keep_set;
	keep_set.insert(keep.begin(), keep.end());
	std::vector<K> removed;
	for (auto& entry : map) {
		if (!contains(keep_set, entry.first)) {
			removed.push_back(entry.first);
		}
	}
	for (auto& key : removed) map.erase(key);
}

template <typename V>
void remove_missing(std::set<V>& set,const std::vector<V>& keep)
{
	std::set<V> keep_set;
	keep_set.insert(keep.begin(), keep.end());
	std::vector<V> removed;
	for (auto& value : set) {
		if (!contains(keep_set, value)) {
			removed.push_back(value);
		}
	}
	for (auto& value : removed) set.erase(value);
}

template <typename T>
bool contains(const std::vector<T>& vector, const T& value)
{
	return std::find(vector.begin(), vector.end(), value) != vector.end();
}

template <typename T>
bool contains(const std::set<T>& set, const T& value)
{
	return set.find(value) != set.end();
}

template <typename T>
bool contains(const std::unordered_set<T>& set, const T& value)
{
	return set.find(value) != set.end();
}

template <typename K,typename V,typename C>
bool contains(const std::map<K,V,C>& map, const K& key)
{
	return map.find(key) != map.end();
}

template <typename T>
const T& clamp(const T& low,const T& value,const T& high)
{
	if (value < low) return low;
	else if (value > high) return high;
	else return value;
}

template <typename T>
int sign(const T& val)
{
	return (T() < val) - (val < T());
}

struct DistanceWithPriority
{
	DistanceWithPriority() : distance(0), priority(0), id(0) {}
	DistanceWithPriority(int distance,int priority,int id) : distance(distance), priority(priority), id(id) {}
	
	int distance;
	int priority;
	int id;
	
	bool operator<(const DistanceWithPriority& o) const
	{
		return std::make_tuple(priority, distance, id) < std::make_tuple(o.priority, o.distance, o.id);
	}
};
