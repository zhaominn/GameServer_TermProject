#include "pch.h"
#include "database_manager.h"

SQLHENV g_henv = NULL;
SQLHDBC g_hdbc = NULL;

DATABASE_MANAGER database_manager;

void DATABASE_MANAGER::HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
	SQLSMALLINT iRec = 0;
	SQLINTEGER iError;
	WCHAR wszMessage[1000];
	WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
}

bool DATABASE_MANAGER::InitODBC_DB()
{
	SQLRETURN ret;

	// 1. 환경 할당  
	ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &g_henv);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
		HandleDiagnosticRecord(g_henv, SQL_HANDLE_ENV, ret); // ★ 추가
		return false;
	}

	// 2. ODBC 버전 설정  
	ret = SQLSetEnvAttr(g_henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
		HandleDiagnosticRecord(g_henv, SQL_HANDLE_ENV, ret);
		return false;
	}

	// 3. DB 커넥션 핸들 할당  
	ret = SQLAllocHandle(SQL_HANDLE_DBC, g_henv, &g_hdbc);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
		HandleDiagnosticRecord(g_hdbc, SQL_HANDLE_DBC, ret);
		return false;
	}

	// 4. 로그인 타임아웃  
	SQLSetConnectAttr(g_hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

	// 5. DB 연결
	SQLRETURN rc = SQLConnect(g_hdbc, (SQLWCHAR*)L"2022180030_GameServer_Project", SQL_NTS,
		NULL, 0, NULL, 0);
	if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
		HandleDiagnosticRecord(g_hdbc, SQL_HANDLE_DBC, rc);
		std::cout << "DB 연결 실패!\n";
		return false;
	}
	std::cout << "DB 연결 성공!\n";
	return true;
}

void DATABASE_MANAGER::CloseODBC_DB()
{
	if (g_hdbc) {
		SQLDisconnect(g_hdbc);
		SQLFreeHandle(SQL_HANDLE_DBC, g_hdbc);
		g_hdbc = NULL;
	}
	if (g_henv) {
		SQLFreeHandle(SQL_HANDLE_ENV, g_henv);
		g_henv = NULL;
	}
	std::cout << "DB 종료!\n";
}

bool DATABASE_MANAGER::get_user_info(
	int user_id, std::string& out_name,
	short& out_x, short& out_y, char& out_dir,
	short& out_max_hp, short& out_hp, short& out_level, int& out_exp
) {
	SQLHSTMT hStmt = nullptr;
	SQLRETURN ret;

	SQLAllocHandle(SQL_HANDLE_STMT, g_hdbc, &hStmt);
	SQLPrepare(hStmt,
		(SQLWCHAR*)L"SELECT USER_NAME, USER_X, USER_Y, USER_DIR, USER_MAX_HP, USER_HP, USER_LEVEL, USER_EXP FROM USER_TABLE WHERE USER_ID=?",
		SQL_NTS);
	SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &user_id, 0, NULL);

	ret = SQLExecute(hStmt);
	if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
		if (SQLFetch(hStmt) == SQL_SUCCESS) {
			char name_buf[64] = {};
			SQLGetData(hStmt, 1, SQL_C_CHAR, name_buf, sizeof(name_buf), NULL); // NAME
			out_name = name_buf;
			SQLGetData(hStmt, 2, SQL_C_SSHORT, &out_x, 0, NULL);
			SQLGetData(hStmt, 3, SQL_C_SSHORT, &out_y, 0, NULL);
			SQLGetData(hStmt, 4, SQL_C_TINYINT, &out_dir, 0, NULL);
			SQLGetData(hStmt, 5, SQL_C_SSHORT, &out_max_hp, 0, NULL);
			SQLGetData(hStmt, 6, SQL_C_SSHORT, &out_hp, 0, NULL);
			SQLGetData(hStmt, 7, SQL_C_SSHORT, &out_level, 0, NULL);
			SQLGetData(hStmt, 8, SQL_C_SLONG, &out_exp, 0, NULL);

			SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
			return true;
		}
	}
	SQLFreeHandle(SQL_HANDLE_STMT, hStmt);

	return false;
}

bool DATABASE_MANAGER::insert_user_info(
	int user_id,
	const std::string& name,
	short x, short y, char dir,
	short max_hp, short hp, short level, int exp
) {
	SQLHSTMT hStmt = nullptr;
	SQLRETURN ret;
	SQLAllocHandle(SQL_HANDLE_STMT, g_hdbc, &hStmt);
	SQLPrepare(hStmt,
		(SQLWCHAR*)L"INSERT INTO USER_TABLE (USER_ID, USER_NAME, USER_X, USER_Y, USER_DIR, "
		L"USER_MAX_HP, USER_HP, USER_LEVEL, USER_EXP) "
		L"VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)", SQL_NTS);
	int p = 1;
	SQLBindParameter(hStmt, p++, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &user_id, 0, NULL);
	SQLBindParameter(hStmt, p++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, name.length(), 0, (SQLPOINTER)name.c_str(), 0, NULL);
	SQLBindParameter(hStmt, p++, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_SMALLINT, 0, 0, &x, 0, NULL);
	SQLBindParameter(hStmt, p++, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_SMALLINT, 0, 0, &y, 0, NULL);
	SQLBindParameter(hStmt, p++, SQL_PARAM_INPUT, SQL_C_TINYINT, SQL_TINYINT, 0, 0, &dir, 0, NULL);
	SQLBindParameter(hStmt, p++, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_SMALLINT, 0, 0, &max_hp, 0, NULL);
	SQLBindParameter(hStmt, p++, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_SMALLINT, 0, 0, &hp, 0, NULL);
	SQLBindParameter(hStmt, p++, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_SMALLINT, 0, 0, &level, 0, NULL);
	SQLBindParameter(hStmt, p++, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &exp, 0, NULL);

	ret = SQLExecute(hStmt);
	SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
	return (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO);
}


bool DATABASE_MANAGER::update_user_info(
	int userid, int x, int y,
	char dir,
	int max_hp, int hp,
	int level, int exp,
	const std::string& name
) {
	SQLHSTMT hStmt = nullptr;
	SQLRETURN ret;

	SQLAllocHandle(SQL_HANDLE_STMT, g_hdbc, &hStmt);

	SQLPrepare(hStmt,
		(SQLWCHAR*)L"UPDATE USER_TABLE "
		L"SET USER_X=?, USER_Y=?, USER_DIR=?, USER_MAX_HP=?, USER_HP=?, USER_LEVEL=?, USER_EXP=?, USER_NAME=? "
		L"WHERE USER_ID=?", SQL_NTS);

	int param = 1;
	SQLBindParameter(hStmt, param++, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, (SQLPOINTER)&x, 0, NULL);
	SQLBindParameter(hStmt, param++, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, (SQLPOINTER)&y, 0, NULL);
	SQLBindParameter(hStmt, param++, SQL_PARAM_INPUT, SQL_C_TINYINT, SQL_TINYINT, 0, 0, (SQLPOINTER)&dir, 0, NULL);
	SQLBindParameter(hStmt, param++, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, (SQLPOINTER)&max_hp, 0, NULL);
	SQLBindParameter(hStmt, param++, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, (SQLPOINTER)&hp, 0, NULL);
	SQLBindParameter(hStmt, param++, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, (SQLPOINTER)&level, 0, NULL);
	SQLBindParameter(hStmt, param++, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, (SQLPOINTER)&exp, 0, NULL);

	// name: std::string, DB: VARCHAR —> SQL_C_CHAR, SQL_VARCHAR!
	SQLBindParameter(hStmt, param++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
		name.length(), 0, (SQLPOINTER)name.c_str(), 0, NULL);

	// user_id: long long, DB: BIGINT
	SQLBindParameter(hStmt, param++, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, (SQLPOINTER)&userid, 0, NULL);

	ret = SQLExecute(hStmt);

	SQLFreeHandle(SQL_HANDLE_STMT, hStmt);

	return (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO);
}
