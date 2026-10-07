class Configuration : public Singleton<Configuration>
{
public:
	void init();
	
	bool human_opponent() const { return human_opponent_; }
	bool draw_enabled() const { return draw_enabled_; }
	bool ucb1() const { return ucb1_; }
	bool tournament() const { return tournament_; }
	
	const std::string& PvZ_opening() const { return PvZ_opening_; }
	const std::string& PvT_opening() const { return PvT_opening_; }
	const std::string& PvP_opening() const { return PvP_opening_; }
	const std::string& PvU_opening() const { return PvU_opening_; }
	
	const std::string& TvZ_opening() const { return TvZ_opening_; }
	const std::string& TvT_opening() const { return TvT_opening_; }
	const std::string& TvP_opening() const { return TvP_opening_; }
	const std::string& TvU_opening() const { return TvU_opening_; }
	
	const std::string& ZvZ_opening() const { return ZvZ_opening_; }
	const std::string& ZvT_opening() const { return ZvT_opening_; }
	const std::string& ZvP_opening() const { return ZvP_opening_; }
	const std::string& ZvU_opening() const { return ZvU_opening_; }
	
private:
	bool human_opponent_ = false;
	bool draw_enabled_ = false;
	bool ucb1_ = false;
	bool tournament_ = false;
	
	std::string PvZ_opening_;
	std::string PvT_opening_;
	std::string PvP_opening_;
	std::string PvU_opening_;
	
	std::string TvZ_opening_;
	std::string TvT_opening_;
	std::string TvP_opening_;
	std::string TvU_opening_;
	
	std::string ZvZ_opening_;
	std::string ZvT_opening_;
	std::string ZvP_opening_;
	std::string ZvU_opening_;
	
	void read_configuration_file(const char* path);
	void apply_key_value(const char *key,const char* value);
};
