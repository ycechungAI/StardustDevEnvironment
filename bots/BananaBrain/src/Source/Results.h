struct Result
{
	std::string timestamp;
	int start_positions;
	int start_clock_position;
	int opponent_clock_position;
	std::string map;
	std::string strategy;
	std::string late_game_strategy;
	std::string opponent_strategy;
	int duration;
	int opponent_dark_templar_frame;
	int opponent_mutalisk_frame;
	int opponent_lurker_frame;
	bool is_win;
};

class ResultStore : public Singleton<ResultStore>
{
public:
	void init();
	std::string pick_strategy(std::vector<std::string> strategies);
	void apply_result(const std::string& strategy,const std::string& late_game_strategy,const std::string& opponent_strategy,bool win);
	void store();
	int minimum_historical_value(std::function<int(const Result&)> value_function,int default_value,size_t min_count,size_t max_count);
	bool historical_exists(std::function<int(const Result&)> predicate_function,size_t max_count);
	
private:
	static constexpr char* kComputerPlayerName = "Computer";
	static constexpr double kDecayFactor = 40.0;
	static constexpr double kDecayFactorTournament = 3.0;
	static constexpr double kTargetWinRate = 0.8;
	static constexpr double kPriorGames = 1.5;
	static constexpr int kMaxHistoricalGames = 200;
	
	char ai_filename_[512];
	char read_filename_[512];
	char write_filename_[512];
	
	std::vector<Result> results_;
	size_t prepared_result_count_;
	
	void init_filenames();
	void read_file();
	void read_prepared_results();
	void read_results();
	void read_file(FILE *f);
	
	std::string pick_strategy_greedy(const std::vector<std::string>& strategies);
	std::string pick_strategy_ucb1(const std::vector<std::string>& strategies);
	static std::string pick_at_random(const std::vector<std::string>& strategies);
	
	static std::string determine_name();
	static int determine_start_positions();
	static std::string determine_map_name();
	static int determine_start_clock_position();
	static int determine_opponent_clock_position();
	static int determine_clock_position(Position position);
	static int determine_opponent_dark_templar_frame();
	static int determine_opponent_mutalisk_frame();
	static int determine_opponent_lurker_frame();
};
