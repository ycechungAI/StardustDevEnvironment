#include "BananaBrain.h"

void Configuration::init()
{
	FILE *f;
	int err;
	
	err = fopen_s(&f, "bwapi-data\\read\\schnail.env", "r");
	if (err == 0) {
		human_opponent_ = true;
		fclose(f);
	}
	
	read_configuration_file("bwapi-data\\AI\\Configuration.txt");
	read_configuration_file("bwapi-data\\read\\Configuration.txt");
}

void Configuration::read_configuration_file(const char* path)
{
	FILE *f;
	int err = fopen_s(&f, path, "r");
	if (err == 0) {
		while (true) {
			char line[1024];
			if (fgets(line, sizeof(line), f) == nullptr) break;
			char key[512];
			char value[512];
			int ret = sscanf(line, "%511[^=]=%511[^\n]\n", key, value);
			if (ret == 2) apply_key_value(key, value);
		}
		
		fclose(f);
	}
}

void Configuration::apply_key_value(const char *key,const char* value)
{
	if (strcmp(key, "draw") == 0) {
		draw_enabled_ = (strcmp(value, "true") == 0);
	} else if (strcmp(key, "ucb1") == 0) {
		ucb1_ = (strcmp(value, "true") == 0);
	} else if (strcmp(key, "tournament") == 0) {
		tournament_ = (strcmp(value, "true") == 0);
	} else if (strcmp(key, "PvZ_opening") == 0) {
		PvZ_opening_ = value;
	} else if (strcmp(key, "PvT_opening") == 0) {
		PvT_opening_ = value;
	} else if (strcmp(key, "PvP_opening") == 0) {
		PvP_opening_ = value;
	} else if (strcmp(key, "PvU_opening") == 0) {
		PvU_opening_ = value;
	} else if (strcmp(key, "TvZ_opening") == 0) {
		TvZ_opening_ = value;
	} else if (strcmp(key, "TvT_opening") == 0) {
		TvT_opening_ = value;
	} else if (strcmp(key, "TvP_opening") == 0) {
		TvP_opening_ = value;
	} else if (strcmp(key, "TvU_opening") == 0) {
		TvU_opening_ = value;
	} else if (strcmp(key, "ZvZ_opening") == 0) {
		ZvZ_opening_ = value;
	} else if (strcmp(key, "ZvT_opening") == 0) {
		ZvT_opening_ = value;
	} else if (strcmp(key, "ZvP_opening") == 0) {
		ZvP_opening_ = value;
	} else if (strcmp(key, "ZvU_opening") == 0) {
		ZvU_opening_ = value;
	}
}
