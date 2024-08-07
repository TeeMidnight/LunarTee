#include <engine/external/json/json.hpp>

#include <game/server/gamecontext.h>

#include <lunartee/datacontroller.h>

#include <thread>

#include "botcontroller.h"

CBotController::CBotController(CGameContext *pGameServer) :
    m_pGameServer(pGameServer)
{
    m_vBotDatas.clear();
}

void CBotController::LoadBotData(std::string Buffer, class CDatapack *pDatapack)
{
    std::thread([this, pDatapack](std::string FileBuffer)
    {
        // parse json data
        nlohmann::json BotData = nlohmann::json::parse(FileBuffer);

        SBotData Data;
        Data.m_Uuid = CalculateUuid(pDatapack, BotData["id"].get<std::string>().c_str());
        Data.m_pSkin = &m_pGameServer->m_TeeSkins[CalculateUuid(pDatapack, BotData["skin-id"].get<std::string>().c_str())];

        Data.m_Health = BotData["health"].get<int>();
        Data.m_AttackProba = BotData["attack_proba"].get<int>();
        Data.m_SpawnProba = BotData["spawn_proba"].get<int>();
        
        Data.m_Flags = 0;
        if(!BotData["gun"].empty() && BotData["gun"].get<bool>())
            Data.m_Flags |= EBotFlags::BOTFLAG_USEGUN;
        if(!BotData["hammer"].empty() && BotData["hammer"].get<bool>())
            Data.m_Flags |= EBotFlags::BOTFLAG_USEHAMMER;
        if(!BotData["hook"].empty() && BotData["hook"].get<bool>())
            Data.m_Flags |= EBotFlags::BOTFLAG_USEHOOK;
        if(!BotData["teamdamage"].empty() && BotData["teamdamage"].get<bool>())
            Data.m_Flags |= EBotFlags::BOTFLAG_TEAMDAMAGE;

        Data.m_Type = BOTTYPE_MONSTER;
        if(BotData["type"].get<std::string>() == "monster")
            Data.m_Type = BOTTYPE_MONSTER;
        if(BotData["type"].get<std::string>() == "resource")
            Data.m_Type = BOTTYPE_RESOURCE;
        if(BotData["type"].get<std::string>() == "trader")
            Data.m_Type = BOTTYPE_TRADER;

        Data.m_Count = 0;
        
        nlohmann::json DropsArray = BotData["drops"];
        if(DropsArray.is_array())
        {
            for(auto &CurrentDrop : DropsArray)
            {
                SBotDropData DropData;
                DropData.m_Uuid = CalculateUuid(pDatapack, CurrentDrop["id"].get<std::string>().c_str());
                DropData.m_DropProba = CurrentDrop["proba"].get<int>();
                DropData.m_MinNum = CurrentDrop["min"].get<int>();
                DropData.m_MaxNum = CurrentDrop["max"].get<int>();
                Data.m_vDrops.push_back(DropData);
            }
        }

        if(Data.m_Type == BOTTYPE_TRADER)
        {
            nlohmann::json TradeArray = BotData["trade"];
            if(TradeArray.is_array())
            {
                for(auto &CurrentTrade : TradeArray)
                {
                    SBotTradeData TradeData;

                    nlohmann::json Current = CurrentTrade["need"];
                    if(!Current.is_array())
                        continue;

                    for(auto &CurrentNeed : Current)
                    {
                        SBotTradeData::SData NeedData;
                        NeedData.m_Uuid = CalculateUuid(pDatapack, CurrentNeed["id"].get<std::string>().c_str());
                        NeedData.m_MinNum = CurrentNeed["min"].get<int>();
                        NeedData.m_MaxNum = CurrentNeed["max"].get<int>();
                        TradeData.m_Needs.push_back(NeedData);
                    }

                    Current = CurrentTrade["give"];
                    if(!Current.is_object())
                        continue;

                    SBotTradeData::SData GiveData;
                    GiveData.m_Uuid = CalculateUuid(pDatapack, Current["id"].get<std::string>().c_str());
                    GiveData.m_MinNum = Current["min"].get<int>();
                    GiveData.m_MaxNum = Current["max"].get<int>();
                    TradeData.m_Give = GiveData;
                    
                    Data.m_vTrade.push_back(TradeData);
                }
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
    
	for(auto& pWorld : GameServer()->m_pWorlds)
	{
        if(pWorld.second->m_Menu)
            continue;

		int NeedSpawn = pWorld.second->Collision()->GetWidth()/16;
		int BotNum = GameServer()->GetBotNum(pWorld.second);
		while(BotNum < NeedSpawn)
		{
			GameServer()->CreateBot(pWorld.second, RandomBotData());

			BotNum ++;
		}
	}

    return;
}

SBotData *CBotController::RandomBotData()
{
	int RandomID;
	do
	{
		RandomID = random_int(0, (int) m_vBotDatas.size()-1);
	}
	while(random_int(1, 100) > m_vBotDatas[RandomID].m_SpawnProba);

    if(m_vBotDatas[RandomID].m_Type == EBotType::BOTTYPE_TRADER && m_vBotDatas[RandomID].m_Count)
    {
        return RandomBotData();
    }

	return &m_vBotDatas[RandomID];
}	

void CBotController::Tick()
{
    OnCreateBot();
}
