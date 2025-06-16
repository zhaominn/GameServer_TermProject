#pragma once
#include "game_header.h"
class DATABASE_MANAGER{
public:
	void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);

	bool InitODBC_DB();

	void CloseODBC_DB();

	bool get_user_info(
		long long user_id, std::string& out_name,
		short& out_x, short& out_y, char& out_dir,
		short& out_max_hp, short& out_hp, short& out_level, int& out_exp);

	bool insert_user_info(
		long long user_id,
		const std::string& name,
		short x, short y, char dir,
		short max_hp, short hp, short level, int exp);

	bool update_user_info(
		long long userid, int x, int y,
		char dir,
		int max_hp, int hp,
		int level, int exp,
		const std::string& name);

};

extern DATABASE_MANAGER database_manager;
