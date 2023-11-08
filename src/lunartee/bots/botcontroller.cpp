#include <engine/external/json/json.hpp>

#include <game/server/gamecontext.h>

#include <thread>

#include "botcontroller.h"

CBotController::CBotController(CGameContext *pGameServer) :
    m_pGameServer(pGameServer)
{
    m_vBotDatas.clear();
}

void CBotController::LoadBotData(std::string Buffer)
{
    std::thread([this](std::string FileBuffer)
    {
        // parse json data
        nlohmann::json BotData = nlohmann::json::parse(FileBuffer);

        CBotData Data;
        str_copy(Data.m_aName, BotData["name"].get<std::string>().c_str());
        str_copy(Data.m_SkinName, BotData["skin"].get<std::string>().c_str());
        if(!BotData["color_body"].empty() && !BotData["color_feet"].empty())
        {
            Data.m_ColorBody = BotData["color_body"].get<int>();
            Data.m_ColorFeet = BotData["color_feet"].get<int>();
        }else
        {
            Data.m_ColorBody = -1;
            Data.m_ColorFeet = -1;
        }
        Data.m_Health = BotData["health"].get<int>();
        Data.m_AttackProba = BotData["attack_proba"].get<int>();
        Data.m_SpawnProba = BotData["spawn_proba"].get<int>();
        Data.m_AI = BotData["AI"].get<bool>();
        Data.m_Gun = BotData["gun"].get<bool>();
        Data.m_Hammer = BotData["hammer"].get<bool>();
        Data.m_Hook = BotData["hook"].get<bool>();
        Data.m_TeamDamage = BotData["teamdamage"].get<bool>();
        
        nlohmann::json DropsArray = BotData["drops"];
        if(DropsArray.is_array())
        {
            for(auto &CurrentDrop : DropsArray)
            {
                CBotDropData DropData;
                str_copy(DropData.m_ItemName, CurrentDrop["name"].get<std::string>().c_str());
                DropData.m_DropProba = CurrentDrop["proba"].get<int>();
                DropData.m_MinNum = CurrentDrop["min"].get<int>();
                DropData.m_MaxNum = CurrentDrop["max"].get<int>();
                Data.m_vDrops.push_back(DropData);
            }
        }

        m_vBotDatas.push_back(Data);
    }, Buffer).detach();
}

void CBotController::OnCreateBot()
{
    if(m_vBotDatas.empty())
    {
        return;
    }
    
	for(int i = 0; i < (int) GameServer()->m_vpWorlds.size(); i ++)
	{
		int NeedSpawn = GameServer()->m_vpWorlds[i]->Collision()->GetWidth()/16;
		int BotNum = GameServer()->GetBotNum(GameServer()->m_vpWorlds[i]);
		while(BotNum < NeedSpawn)
		{	
			CBotData Data = RandomBotData();
			GameServer()->CreateBot(GameServer()->m_vpWorlds[i], Data);

			BotNum ++;
		}
	}

    return;
}

CBotData CBotController::RandomBotData()
{
	CBotData Data;
	int RandomID;
	do
	{
		RandomID = random_int(0, (int) m_vBotDatas.size()-1);
	}
	while(random_int(1, 100) > m_vBotDatas[RandomID].m_SpawnProba);
	Data = m_vBotDatas[RandomID];
	return Data;
}	

void CBotController::Tick()
{
    OnCreateBot();
}
