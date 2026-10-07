#include "BananaBrain.h"

void ResultStore::init()
{
	init_filenames();
	read_prepared_results();
	prepared_result_count_ = results_.size();
	read_results();
}

void ResultStore::init_filenames()
{
	sprintf_s(ai_filename_, sizeof(ai_filename_), "bwapi-data\\AI\\Results_%s.txt", determine_name().c_str());
	sprintf_s(read_filename_, sizeof(read_filename_), "bwapi-data\\read\\Results_%s.txt", determine_name().c_str());
	sprintf_s(write_filename_, sizeof(write_filename_), "bwapi-data\\write\\Results_%s.txt", determine_name().c_str());
}

void ResultStore::read_prepared_results()
{
	FILE *f;
	int err = fopen_s(&f, ai_filename_, "r");
	if (err == 0) read_file(f);
}

void ResultStore::read_results()
{
	FILE *f;
	int err = fopen_s(&f, read_filename_, "r");
	if (err != 0) err = fopen_s(&f, write_filename_, "r");
	if (err == 0) read_file(f);
}

void ResultStore::read_file(FILE *f)
{
	while (true) {
		char timestamp[512];
		int start_positions;
		int start_clock_position;
		int opponent_clock_position;
		char map[512];
		char strategy[512];
		char late_game_strategy[512];
		char opponent_strategy[512];
		int duration;
		int opponent_dark_templar_frame;
		int opponent_mutalisk_frame;
		int opponent_lurker_frame;
		int is_win;
		int ret = fscanf(f, "%511[^,],%d,%d,%d,%511[^,],%511[^,],%511[^,],%511[^,],%d,%d,%d,%d,%d\n",
						   timestamp,
						   &start_positions, &start_clock_position, &opponent_clock_position,
						   map,
						   strategy,
						   late_game_strategy,
						   opponent_strategy,
						   &duration, &opponent_dark_templar_frame, &opponent_mutalisk_frame, &opponent_lurker_frame,
						   &is_win);
		if (ret == EOF) break;
		if (ret == 13) {
			Result result;
			result.timestamp = timestamp;
			result.start_positions = start_positions;
			result.start_clock_position = start_clock_position;
			result.opponent_clock_position = opponent_clock_position;
			result.map = map;
			result.strategy = strategy;
			result.late_game_strategy = late_game_strategy;
			result.opponent_strategy = opponent_strategy;
			result.duration = duration;
			result.opponent_dark_templar_frame = opponent_dark_templar_frame;
			result.opponent_mutalisk_frame = opponent_mutalisk_frame;
			result.opponent_lurker_frame = opponent_lurker_frame;
			result.is_win = (is_win != 0);
			results_.push_back(std::move(result));
		}
	}
	fclose(f);
}

std::string ResultStore::pick_strategy(std::vector<std::string> strategies)
{
	if (configuration.human_opponent() || configuration.ucb1()) {
		return pick_strategy_ucb1(strategies);
	} else {
		return pick_strategy_greedy(strategies);
	}
}

std::string ResultStore::pick_strategy_greedy(const std::vector<std::string>& strategies)
{
	struct StrategyRecord {
		int win_count = 0;
		int loss_count = 0;
		
		bool is_unplayed() { return win_count == 0 && loss_count == 0; }
		bool is_win_only() { return win_count >= 1 && loss_count == 0; }
	};
	
	std::set<std::string> strategies_set(strategies.begin(), strategies.end());
	std::map<std::string,StrategyRecord> map;
	for (auto &result : results_) {
		if (contains(strategies_set, result.strategy)) {
			auto& record = map[result.strategy];
			if (result.is_win) record.win_count++; else record.loss_count++;
		}
	}
	std::vector<std::string> unplayed_strategies;
	for (auto& strategy : strategies) {
		if (map[strategy].is_unplayed()) unplayed_strategies.push_back(strategy);
	}
	if (!unplayed_strategies.empty()) {
		std::vector<std::string> win_only_stratgies;
		for (auto& strategy : strategies) {
			if (map[strategy].is_win_only()) {
				win_only_stratgies.push_back(strategy);
			}
		}
		if (!win_only_stratgies.empty()) return pick_at_random(win_only_stratgies);
		
		return pick_at_random(unplayed_strategies);
	}
	
	std::map<std::string,std::vector<bool>> result_map;
	size_t count = 0;
	for (auto it = results_.rbegin(); it != results_.rend() && (configuration.tournament() || count < kMaxHistoricalGames); ++it) {
		auto& result = *it;
		if (contains(strategies_set, result.strategy)) {
			result_map[result.strategy].push_back(result.is_win);
			count++;
		}
	}
	
	const double decay_factor = configuration.tournament() ? kDecayFactorTournament : kDecayFactor;
	std::map<std::string,double> estimator_map;
	for (auto& strategy : strategies) {
		const std::vector<bool>& results = result_map[strategy];
		double weight_sum = 0.0;
		double won_weight_sum = 0.0;
		for (size_t i = 0; i < results.size(); i++) {
			size_t index = i + 1;
			double weight = 1.0 / (1.0 + (index / decay_factor));
			weight_sum += weight;
			if (results[i]) won_weight_sum += weight;
		}
		estimator_map[strategy] = (kPriorGames * kTargetWinRate + won_weight_sum) / (kPriorGames + weight_sum);
	}
	// @
	/*FILE *f = fopen("bwapi-data\\write\\greedyscores.txt", "w");
	for (auto& strategy : strategies) {
		fprintf(f, "%s %d %.2f%%\n", strategy.c_str(), int(result_map[strategy].size()), 100.0 * estimator_map[strategy]);
	}
	fclose(f);*/
	// /@
	auto it = std::max_element(estimator_map.begin(), estimator_map.end(), [](auto& entry1,auto& entry2){
		return entry1.second < entry2.second;
	});
	double max_estimator = (*it).second;
	
	std::vector<std::string> potential_strategies;
	for (auto& entry : estimator_map) {
		if (entry.second == max_estimator) potential_strategies.push_back(entry.first);
	}
	return pick_at_random(potential_strategies);
}

std::string ResultStore::pick_strategy_ucb1(const std::vector<std::string>& strategies)
{
	struct StrategyRecord
	{
		int win_count = 0;
		int loss_count = 0;
		double score = 0.0;
		
		bool empty() { return win_count == 0 && loss_count == 0; }
		int count() { return win_count + loss_count; }
	};
	
	std::map<std::string,StrategyRecord> map;
	for (auto& strategy : strategies) {
		StrategyRecord& record = map[strategy];
		for (auto& result : results_) {
			if (result.strategy == strategy) {
				if (result.is_win) record.win_count++; else record.loss_count++;
			}
		}
	}
	
	std::vector<std::string> unplayed_strategies;
	for (auto& strategy : strategies) {
		if (map[strategy].empty()) unplayed_strategies.push_back(strategy);
	}
	if (!unplayed_strategies.empty()) return pick_at_random(unplayed_strategies);
	
	int n = 0;
	for (auto& strategy : strategies) {
		StrategyRecord& result = map[strategy];
		n += result.count();
	}
	double twologn = 2.0 * std::log(n);
	for (auto& strategy : strategies) {
		StrategyRecord& record = map[strategy];
		double wins = record.win_count;
		double attempts = record.count();
		double average_reward = wins / attempts;
		record.score = average_reward + std::sqrt(twologn / attempts);
	}
	
	auto it = std::max_element(map.begin(), map.end(), [](auto& entry1,auto &entry2) {
		return entry1.second.score < entry2.second.score;
	});
	double max_score = (*it).second.score;
	
	std::vector<std::string> potential_strategies;
	for (auto& strategy : strategies) if (map[strategy].score == max_score) potential_strategies.push_back(strategy);
	return pick_at_random(potential_strategies);
}

std::string ResultStore::pick_at_random(const std::vector<std::string>& strategies)
{
	if (strategies.size() > 1) {
		std::uniform_int_distribution<size_t> dist(0, strategies.size() - 1);
		int r = dist(random_generator());
		return strategies[r];
	} else {
		return strategies[0];
	}
}

void ResultStore::apply_result(const std::string& strategy,const std::string& late_game_strategy,const std::string& opponent_strategy,bool win)
{
	Result result;
	
	char timestamp[80];
	time_t rawtime;
	time(&rawtime);
	struct tm timeinfo;
	gmtime_s(&timeinfo, &rawtime);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
	
	result.timestamp = timestamp;
	result.start_positions = determine_start_positions();
	result.start_clock_position = determine_start_clock_position();
	result.opponent_clock_position = determine_opponent_clock_position();
	result.map = determine_map_name();
	result.strategy = strategy;
	result.late_game_strategy = late_game_strategy;
	result.opponent_strategy = opponent_strategy;
	result.duration = Broodwar->getFrameCount();
	result.opponent_dark_templar_frame = determine_opponent_dark_templar_frame();
	result.opponent_mutalisk_frame = determine_opponent_mutalisk_frame();
	result.opponent_lurker_frame = determine_opponent_lurker_frame();
	result.is_win = win;
	
	results_.push_back(std::move(result));
}

void ResultStore::store()
{
	FILE *f;
	int err = fopen_s(&f, write_filename_, "w");
	if (err == 0) {
		for (auto it = results_.cbegin() + prepared_result_count_; it != results_.cend(); ++it) {
			const auto& result = *it;
			fprintf(f, "%s,%d,%d,%d,%s,%s,%s,%s,%d,%d,%d,%d,%d\n",
					result.timestamp.c_str(),
					result.start_positions, result.start_clock_position, result.opponent_clock_position,
					result.map.c_str(),
					result.strategy.c_str(), result.late_game_strategy.c_str(), result.opponent_strategy.c_str(),
					result.duration, result.opponent_dark_templar_frame, result.opponent_mutalisk_frame, result.opponent_lurker_frame,
					result.is_win ? 1 : 0);
		}
		fclose(f);
	}
}

std::string ResultStore::determine_name()
{
	if (Broodwar->enemy()->getType() != PlayerTypes::Computer) {
		return Broodwar->enemy()->getName();
	} else {
		return kComputerPlayerName;
	}
}

int ResultStore::determine_start_positions()
{
	return base_state.start_base_count();
}

std::string ResultStore::determine_map_name()
{
	return Broodwar->mapFileName();
}

int ResultStore::determine_start_clock_position()
{
	return determine_clock_position(base_state.start_base()->Center());
}

int ResultStore::determine_opponent_clock_position()
{
	int result = -1;
	if (tactics_manager.enemy_start_base() != nullptr) {
		result = determine_clock_position(tactics_manager.enemy_start_base()->Center());
	} else {
		Position position = tactics_manager.enemy_start_position();
		if (position.isValid()) {
			result = determine_clock_position(position);
		}
	}
	return result;
}

int ResultStore::determine_clock_position(Position position)
{
	const auto transform = [](int value,int max){
		double s = double(value) / double(max);
		return 2.0 * s - 1.0;
	};
	double x = transform(position.x, 32 * Broodwar->mapWidth());
	double y = transform(position.y, 32 * Broodwar->mapHeight());
	double theta = std::atan2(x, -y);
	const double part_angle = 2.0 * M_PI / 12.0;
	theta -= (part_angle / 2.0);
	if (theta < 0.0) {
		theta += (2.0 * M_PI);
	}
	return 1 + int(theta / (2.0 * M_PI / 12.0));
}

int ResultStore::determine_opponent_dark_templar_frame()
{
	int result = -1;
	if (opponent_model.dark_templar_frame() >= 0) {
		int min_distance = INT_MAX;
		for (auto& base : tactics_manager.possible_enemy_start_bases()) {
			int distance = ground_distance(opponent_model.dark_templar_position(), base->Center());
			if (distance > 0) min_distance = std::min(min_distance, distance);
		}
		if (min_distance < INT_MAX) {
			result = opponent_model.dark_templar_frame() - int(min_distance / UnitTypes::Protoss_Dark_Templar.topSpeed() + 0.5);
		}
	}
	return result;
}

int ResultStore::determine_opponent_mutalisk_frame()
{
	int result = -1;
	if (opponent_model.mutalisk_frame() >= 0) {
		int min_distance = INT_MAX;
		for (auto& base : tactics_manager.possible_enemy_start_bases()) {
			int distance = opponent_model.mutalisk_position().getApproxDistance(base->Center());
			min_distance = std::min(min_distance, distance);
		}
		if (min_distance < INT_MAX) {
			result = opponent_model.mutalisk_frame() - int(min_distance / UnitTypes::Zerg_Mutalisk.topSpeed() + 0.5);
		}
	}
	return result;
}

int ResultStore::determine_opponent_lurker_frame()
{
	int result = -1;
	if (opponent_model.lurker_frame() >= 0) {
		result = opponent_model.lurker_frame();
	}
	return result;
}

int ResultStore::minimum_historical_value(std::function<int(const Result&)> value_function,int default_value,size_t min_count,size_t max_count)
{
	int result;
	
	if (results_.size() >= min_count) {
		result = INT_MAX;
		size_t count = 0;
		for (auto it = results_.rbegin(); it != results_.rend(); ++it) {
			if (count >= max_count) break;
			result = std::min(result, value_function(*it));
			count++;
		}
	} else {
		result = default_value;
	}
	
	return result;
}

bool ResultStore::historical_exists(std::function<int(const Result&)> predicate_function,size_t max_count)
{
	size_t count = 0;
	for (auto it = results_.rbegin(); it != results_.rend(); ++it) {
		if (count++ >= max_count) break;
		if (predicate_function(*it)) return true;
	}
	return false;
}
