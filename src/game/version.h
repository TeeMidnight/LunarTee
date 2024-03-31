/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */


#ifndef GAME_VERSION_H
#define GAME_VERSION_H
#ifndef NON_HASED_VERSION
#include "generated/nethash.cpp"

#define GAME_RELEASE_VERSION "0.6.4"

#define MOD_NAME "LunarTee"
#define MOD_VERSION "b0.1.4"

#define GAME_VERSION GAME_RELEASE_VERSION ", " MOD_NAME " " MOD_VERSION
#define GAME_NETVERSION "0.6 626fce9a778df4d4" //the std game version

#define DDNET_VERSIONNR 17100
#define DATAPACK_VERSION 3

#endif
#endif
