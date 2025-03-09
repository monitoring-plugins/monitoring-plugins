#pragma once
#include "../../config.h"
#include <stddef.h>

typedef struct {
	char *server_ip;
	char *game_type;
	int port;

	int qstat_game_players_max;
	int qstat_game_players;
	int qstat_game_field;
	int qstat_map_field;
	int qstat_ping_field;
} check_game_config;

check_game_config check_game_config_init() {
	check_game_config tmp = {
		.server_ip = NULL,
		.game_type = NULL,
		.port = 0,

		.qstat_game_players_max = 4,
		.qstat_game_players = 5,
		.qstat_map_field = 3,
		.qstat_game_field = 2,
		.qstat_ping_field = 5,
	};
	return tmp;
}
