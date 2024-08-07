#ifndef GAME_SERVER_TEEINFO_H
#define GAME_SERVER_TEEINFO_H

#include <generated/protocol7.h>

class CTeeInfo
{
public:
	constexpr static const float ms_DarkestLGT7 = 61 / 255.0f;

	char m_aSkinName[24] = {'\0'};
	int m_UseCustomColor = 0;
	int m_ColorBody = 0;
	int m_ColorFeet = 0;

	// 0.7
	char m_apSkinPartNames[protocol7::NUM_SKINPARTS][24] = {"", "", "", "", "", ""};
	bool m_aUseCustomColors[protocol7::NUM_SKINPARTS] = {false, false, false, false, false, false};
	int m_aSkinPartColors[protocol7::NUM_SKINPARTS] = {0, 0, 0, 0, 0, 0};

	CTeeInfo();

	CTeeInfo(const char *pSkinName, int UseCustomColor, int ColorBody, int ColorFeet);

	// This constructor will assume all arrays are of length 6
	CTeeInfo(const char *apSkinPartNames[protocol7::NUM_SKINPARTS], const int *pUseCustomColors, const int *pSkinPartColors);

	void FromSixup();
	void ToSixup();
};
#endif //GAME_SERVER_TEEINFO_H
