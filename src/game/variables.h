/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_VARIABLES_H
#define GAME_VARIABLES_H
#undef GAME_VARIABLES_H // this file will be included several times

MACRO_CONFIG_INT(SvGeneratedMap, sv_generated_map, 1, 0, 1, CFGFLAG_SERVER, "regenerate the generated map")

MACRO_CONFIG_STR(SvSqlDatabase, sv_sql_database, 256, "db_lunartee", CFGFLAG_SERVER, "SQL Database name")
MACRO_CONFIG_STR(SvSqlUser, sv_sql_user, 256, "postgres", CFGFLAG_SERVER, "SQL User")
MACRO_CONFIG_STR(SvSqlPass, sv_sql_pass, 256, "passless", CFGFLAG_SERVER, "SQL Password")
MACRO_CONFIG_STR(SvSqlIP, sv_sql_ip, 256, "127.0.0.1", CFGFLAG_SERVER, "SQL Database IP")
MACRO_CONFIG_INT(SvSqlPort, sv_sql_port, 5432, 0, 65535, CFGFLAG_SERVER, "SQL Database port")
#endif
