/*
 * Copyright (C) 2008 - 2011 Trinity <http://www.trinitycore.org/>
 *
 * Copyright (C) 2010 - 2014 Myth Project <http://mythprojectnetwork.blogspot.com/>
 *
 * Myth Project's source is based on the Trinity Project source, you can find the
 * link to that easily in Trinity Copyrights. Myth Project is a private community.
 * To get access, you either have to donate or pass a developer test.
 * You may not share Myth Project's sources! For personal use only.
 */

#include "HMACSHA1.h"
#include "WardenKeyGeneration.h"
#include "Common.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Log.h"
#include "Opcodes.h"
#include "ByteBuffer.h"
#include <openssl/md5.h>
#include "Database/DatabaseEnv.h"
#include "World.h"
#include "Config.h"
#include "Player.h"
#include "Util.h"
#include "WardenWin.h"
#include "WardenModuleWin.h"
#include "WardenDataStorage.h"

CWardenDataStorage WardenDataStorage;

WardenWin::WardenWin() { }

WardenWin::~WardenWin() { }

void WardenWin::Init(WorldSession* pClient, BigNumber* K)
{
    Client = pClient;
    // Generate Warden Key
    SHA1Randx WK(K->AsByteArray(), K->GetNumBytes());
    WK.generate(InputKey, 16);
    WK.generate(OutputKey, 16);

    uint8 mod_seed[16] = { 0x4D, 0x80, 0x8D, 0x2C, 0x77, 0xD9, 0x05, 0xC4, 0x1A, 0x63, 0x80, 0xEC, 0x08, 0x58, 0x6A, 0xFE };

    memcpy(Seed, mod_seed, 16);

    iCrypto.Init(InputKey);
    oCrypto.Init(OutputKey);
    sLog->outDebug(LOG_FILTER_UNITS, "Server side warden for client %u initializing...", pClient->GetAccountId());
    //PrintHexArray("  C->S Key: ", InputKey, 16, true);
    //PrintHexArray("  S->C Key: ", OutputKey, 16, true);
    //PrintHexArray("  Seed: ", Seed, 16, true);
    sLog->outDebug(LOG_FILTER_UNITS, "Loading Module...");

    Module = GetModuleForClient();

    //PrintHexArray("  Module Key: ", Module->Key, 16, true);
    //PrintHexArray("  Module ID: ", Module->ID, 16, true);
    RequestModule();
}

ClientWardenModule* WardenWin::GetModuleForClient()
{
    ClientWardenModule* mod = new ClientWardenModule;

    uint32 len = sizeof(Module_79C0768D657977D697E10BAD956CCED1_Data);

    // data assign
    mod->CompressedSize = len;
    mod->CompressedData = new uint8[len];
    memcpy(mod->CompressedData, Module_79C0768D657977D697E10BAD956CCED1_Data, len);
    memcpy(mod->Key, Module_79C0768D657977D697E10BAD956CCED1_Key, 16);

    // md5 hash
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, mod->CompressedData, len);
    MD5_Final((uint8*)&mod->ID, &ctx);

    return mod;
}

void WardenWin::InitializeModule()
{
    sLog->outDebug(LOG_FILTER_UNITS, "Initialize module");

    // Create packet structure
    WardenInitModuleRequest Request;
    Request.Command1 = WARDEN_SMSG_MODULE_INITIALIZE;
    Request.Size1 = 20;
    Request.CheckSumm1 = BuildChecksum(&Request.Unk1, 20);
    Request.Unk1 = 1;
    Request.Unk2 = 0;
    Request.Type = 1;
    Request.String_library1 = 0;
    Request.Function1[0] = 0x00024F80;                      // 0x00400000 + 0x00024F80 SFileOpenFile
    Request.Function1[1] = 0x000218C0;                      // 0x00400000 + 0x000218C0 SFileGetFileSize
    Request.Function1[2] = 0x00022530;                      // 0x00400000 + 0x00022530 SFileReadFile
    Request.Function1[3] = 0x00022910;                      // 0x00400000 + 0x00022910 SFileCloseFile

    Request.Command2 = WARDEN_SMSG_MODULE_INITIALIZE;
    Request.Size2 = 8;
    Request.CheckSumm2 = BuildChecksum(&Request.Unk2, 8);
    Request.Unk3 = 4;
    Request.Unk4 = 0;
    Request.String_library2 = 0;
    Request.Function2 = 0x00419D40;                         // 0x00400000 + 0x00419D40 FrameScript::GetText
    Request.Function2_set = 1;

    Request.Command3 = WARDEN_SMSG_MODULE_INITIALIZE;
    Request.Size3 = 8;
    Request.CheckSumm3 = BuildChecksum(&Request.Unk5, 8);
    Request.Unk5 = 1;
    Request.Unk6 = 1;
    Request.String_library3 = 0;
    Request.Function3 = 0x0046AE20;                         // 0x00400000 + 0x0046AE20 PerformanceCounter
    Request.Function3_set = 1;

    // Encrypt with warden RC4 key.
    EncryptData((uint8*)&Request, sizeof(WardenInitModuleRequest));

    WorldPacket pkt(SMSG_WARDEN_DATA, sizeof(WardenInitModuleRequest));
    pkt.append((uint8*)&Request, sizeof(WardenInitModuleRequest));
    Client->SendPacket(&pkt);
}

void WardenWin::RequestHash()
{
    sLog->outDebug(LOG_FILTER_UNITS, "Request hash");

    // Create packet structure
    WardenHashRequest Request;
    Request.Command = WARDEN_SMSG_HASH_REQUEST;
    memcpy(Request.Seed, Seed, 16);

    // Encrypt with warden RC4 key.
    EncryptData((uint8*)&Request, sizeof(WardenHashRequest));

    WorldPacket pkt(SMSG_WARDEN_DATA, sizeof(WardenHashRequest));
    pkt.append((uint8*)&Request, sizeof(WardenHashRequest));
    Client->SendPacket(&pkt);
}

void WardenWin::HandleHashResult(ByteBuffer &buff)
{
    buff.rpos(buff.wpos());

    const uint8 validHash[20] = { 0x56, 0x8C, 0x05, 0x4C, 0x78, 0x1A, 0x97, 0x2A, 0x60, 0x37, 0xA2, 0x29, 0x0C, 0x22, 0xB5, 0x25, 0x71, 0xA0, 0x6F, 0x4E };

    // verify key not equal kick player
    if(memcmp(buff.contents() + 1, validHash, sizeof(validHash)) != 0)
    {
        sLog->outWarden("Request hash reply: failed");
        if(sWorld->getBoolConfig(CONFIG_BOOL_WARDEN_KICK))
            Client->KickPlayer();
        return;
    }

    sLog->outDebug(LOG_FILTER_UNITS, "Request hash reply: succeed");

    // client 7F96EEFDA5B63D20A4DF8E00CBF48304
    const uint8 client_key[16] = { 0x7F, 0x96, 0xEE, 0xFD, 0xA5, 0xB6, 0x3D, 0x20, 0xA4, 0xDF, 0x8E, 0x00, 0xCB, 0xF4, 0x83, 0x04 };

    // server C2B7ADEDFCCCA9C2BFB3F85602BA809B
    const uint8 server_key[16] = { 0xC2, 0xB7, 0xAD, 0xED, 0xFC, 0xCC, 0xA9, 0xC2, 0xBF, 0xB3, 0xF8, 0x56, 0x02, 0xBA, 0x80, 0x9B };

    // change keys here
    memcpy(InputKey, client_key, 16);
    memcpy(OutputKey, server_key, 16);

    iCrypto.Init(InputKey);
    oCrypto.Init(OutputKey);

    m_initialized = true;

    m_WardenTimer = getMSTime();
}

void WardenWin::RequestData()
{
    sLog->outDebug(LOG_FILTER_UNITS, "Request data");

    if(MemCheck.empty())
        MemCheck.assign(WardenDataStorage.MemCheckIds.begin(), WardenDataStorage.MemCheckIds.end());

    ServerTicks = getMSTime();

    uint32 maxid = WardenDataStorage.InternalDataID;

    uint32 id;
    uint8 type;
    WardenData *wd;

    SendDataId.clear();

    uint8 memcheckAdded = 0;
    for(uint32 i = 1; i <= maxid; ++i)
    {
        if(MemCheck.empty())
            break;

        if(memcheckAdded >= 3)  // for now include 3 MEM_CHECK's
            break;

        id = MemCheck.back();

        if(std::find(SendDataId.begin(), SendDataId.end(), id) != SendDataId.end())
            continue;

        memcheckAdded++;
        SendDataId.push_back(id);
        MemCheck.pop_back();
    }

    ByteBuffer buff;
    buff << uint8(WARDEN_SMSG_CHEAT_CHECKS_REQUEST);

    uint8 cheatCheckAdded = 0;
    for(uint32 i = 1; i <= maxid; ++i)                             // for now include 5 random checks
    {
        if(cheatCheckAdded >= 5)
            break;

        id = urand(1, maxid - 1);

        if(std::find(SendDataId.begin(), SendDataId.end(), id) != SendDataId.end())
            continue;

        cheatCheckAdded++;
        wd = WardenDataStorage.GetWardenDataById(id);
        SendDataId.push_back(id);
        switch(wd->Type)
        {
            case MPQ_CHECK:
            case LUA_STR_CHECK:
            case DRIVER_CHECK:
                buff << uint8(wd->str.size());
                buff.append(wd->str.c_str(), wd->str.size());
                break;
            default:
                break;
        }
    }

    uint8 xorByte = InputKey[0];

    buff << uint8(0x00);
    buff << uint8(TIMING_CHECK ^ xorByte);                  // check TIMING_CHECK

    uint8 index = 1;

    for(std::vector<uint32>::iterator itr = SendDataId.begin(); itr != SendDataId.end(); ++itr)
    {
        wd = WardenDataStorage.GetWardenDataById(*itr);

        type = wd->Type;
        buff << uint8(type ^ xorByte);
        switch(type)
        {
            case MEM_CHECK:
            {
                buff << uint8(0x00);
                buff << uint32(wd->Address);
                buff << uint8(wd->Length);
                break;
            }
            case PAGE_CHECK_A:
            case PAGE_CHECK_B:
            {
                buff.append(wd->i.AsByteArray(0, false), wd->i.GetNumBytes());
                buff << uint32(wd->Address);
                buff << uint8(wd->Length);
                break;
            }
            case MPQ_CHECK:
            case LUA_STR_CHECK:
            {
                buff << uint8(index++);
                break;
            }
            case DRIVER_CHECK:
            {
                buff.append(wd->i.AsByteArray(0, false), wd->i.GetNumBytes());
                buff << uint8(index++);
                break;
            }
            case MODULE_CHECK:
            {
                uint32 seed = static_cast<uint32>(rand32());
                buff << uint32(seed);
                HmacHash hmac(4, (uint8*)&seed);
                hmac.UpdateData(wd->str);
                hmac.Finalize();
                buff.append(hmac.GetDigest(), hmac.GetLength());
                break;
            }
            default:
                break;                                      // should never happens
        }
    }
    buff << uint8(xorByte);
    buff.hexlike();

    // Encrypt with warden RC4 key.
    EncryptData(const_cast<uint8*>(buff.contents()), buff.size());

    WorldPacket pkt(SMSG_WARDEN_DATA, buff.size());
    pkt.append(buff);
    Client->SendPacket(&pkt);

    m_WardenDataSent = true;

    std::stringstream stream;
    stream << "Sent check id's: ";
    for(std::vector<uint32>::iterator itr = SendDataId.begin(); itr != SendDataId.end(); ++itr)
        stream << *itr << " ";
    sLog->outDebug(LOG_FILTER_UNITS, stream.str().c_str());
}

void WardenWin::HandleData(ByteBuffer &buff)
{
    sLog->outDebug(LOG_FILTER_UNITS, "Handle data");

    m_WardenDataSent = false;
    m_WardenKickTimer = 0;
    bool forceKick = false;

    uint16 Length;
    buff >> Length;
    uint32 Checksum;
    buff >> Checksum;

    if(!IsValidCheckSum(Checksum, buff.contents() + buff.rpos(), Length))
    {
        buff.rpos(buff.wpos());
        if(sWorld->getBoolConfig(CONFIG_BOOL_WARDEN_KICK))
            Client->KickPlayer();
        return;
    }

    bool found = false;
    uint8 banDays = 0;

    //TIMING_CHECK
    {
        uint8 result;
        buff >> result;
        // TODO: test it.
        if(result == 0x00)
        {
            sLog->outWarden("TIMING CHECK FAIL result 0x00");
            found = true;
        }

        uint32 newClientTicks;
        buff >> newClientTicks;

        uint32 ticksNow = getMSTime();
        uint32 ourTicks = newClientTicks + (ticksNow - ServerTicks);

        sLog->outDebug(LOG_FILTER_UNITS, "ServerTicks %u", ticksNow);         // now
        sLog->outDebug(LOG_FILTER_UNITS, "RequestTicks %u", ServerTicks);     // at request
        sLog->outDebug(LOG_FILTER_UNITS, "Ticks %u", newClientTicks);         // at response
        sLog->outDebug(LOG_FILTER_UNITS, "Ticks diff %u", ourTicks - newClientTicks);
    }

    WardenDataResult *rs;
    WardenData *rd;
    uint8 type;

    for(std::vector<uint32>::iterator itr = SendDataId.begin(); itr != SendDataId.end(); ++itr)
    {
        if(forceKick)
            break;

        rd = WardenDataStorage.GetWardenDataById(*itr);
        rs = WardenDataStorage.GetWardenResultById(*itr);

        type = rd->Type;
        switch(type)
        {
            case MEM_CHECK:
            {
                uint8 Mem_Result;
                buff >> Mem_Result;

                if(Mem_Result != 0)
                {
                    sLog->outWarden("RESULT MEM_CHECK not 0x00, CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                    forceKick = true;
                    banDays = 0;
                    found = true;

                    continue;
                }

                if(memcmp(buff.contents() + buff.rpos(), rs->res.AsByteArray(0, false), rd->Length) != 0)
                {
                    sLog->outWarden("RESULT MEM_CHECK fail CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                    found = true;

                    if(rd->banDays > banDays)
                        banDays = rd->banDays;

                    buff.rpos(buff.rpos() + rd->Length);
                    continue;
                }

                buff.rpos(buff.rpos() + rd->Length);
                sLog->outDebug(LOG_FILTER_NETWORKIO, "RESULT MEM_CHECK passed CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                break;
            }
            case PAGE_CHECK_A:
            case PAGE_CHECK_B:
            case DRIVER_CHECK:
            case MODULE_CHECK:
            {
                const uint8 byte = 0xE9;
                if(memcmp(buff.contents() + buff.rpos(), &byte, sizeof(uint8)) != 0)
                {
                    if(type == PAGE_CHECK_A || type == PAGE_CHECK_B)
                        sLog->outWarden("RESULT PAGE_CHECK fail, CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                    if(type == MODULE_CHECK)
                        sLog->outWarden("RESULT MODULE_CHECK fail, CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                    if(type == DRIVER_CHECK)
                        sLog->outWarden("RESULT DRIVER_CHECK fail, CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                    found = true;
                    buff.rpos(buff.rpos() + 1);

                    if(rd->banDays > banDays)
                        banDays = rd->banDays;

                    continue;
                }

                buff.rpos(buff.rpos() + 1);
                if(type == PAGE_CHECK_A || type == PAGE_CHECK_B)
                    sLog->outDebug(LOG_FILTER_UNITS, "RESULT PAGE_CHECK passed CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                else if(type == MODULE_CHECK)
                    sLog->outDebug(LOG_FILTER_UNITS, "RESULT MODULE_CHECK passed CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                else if(type == DRIVER_CHECK)
                    sLog->outDebug(LOG_FILTER_UNITS, "RESULT DRIVER_CHECK passed CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                break;
            }
            case LUA_STR_CHECK:
            {
                uint8 Lua_Result;
                buff >> Lua_Result;

                if(Lua_Result != 0)
                {
                    sLog->outWarden("RESULT LUA_STR_CHECK fail, CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                    found = true;

                    if(rd->banDays > banDays)
                        banDays = rd->banDays;

                    continue;
                }

                uint8 luaStrLen;
                buff >> luaStrLen;

                if(luaStrLen != 0)
                {
                    char *str = new char[luaStrLen + 1];
                    memset(str, 0, luaStrLen + 1);
                    memcpy(str, buff.contents() + buff.rpos(), luaStrLen);
                    sLog->outDebug(LOG_FILTER_UNITS, "Lua string: %s", str);
                    delete[] str;
                }
                buff.rpos(buff.rpos() + luaStrLen);         // skip string
                sLog->outDebug(LOG_FILTER_UNITS, "RESULT LUA_STR_CHECK passed, CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                break;
            }
            case MPQ_CHECK:
            {
                uint8 Mpq_Result;
                buff >> Mpq_Result;

                if(Mpq_Result != 0)
                {
                    sLog->outWarden("RESULT MPQ_CHECK not 0x00 account id %u", Client->GetAccountId());
                    found = true;

                    if(rd->banDays > banDays)
                        banDays = rd->banDays;

                    continue;
                }

                if(memcmp(buff.contents() + buff.rpos(), rs->res.AsByteArray(0, false), 20) != 0) // SHA1
                {
                    sLog->outWarden("RESULT MPQ_CHECK fail, CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                    found = true;
                    buff.rpos(buff.rpos() + 20);            // 20 bytes SHA1

                    if(rd->banDays > banDays)
                        banDays = rd->banDays;

                    continue;
                }

                buff.rpos(buff.rpos() + 20);                // 20 bytes SHA1
                sLog->outDebug(LOG_FILTER_UNITS, "RESULT MPQ_CHECK passed, CheckId: %u, account Id: %u, player: %s", rd->DBid, Client->GetAccountId(), Client->GetPlayerName());
                break;
            }
            default:                                        // should never happens
                break;
        }
    }

    if(banDays > 0 &&  m_previousCheckFailed)
        banDays = 0;

    m_previousCheckFailed = false;

    if(found && banDays > 0 && sWorld->getIntConfig(CONFIG_INT_WARDEN_BANDAY) > 0)
    {
        std::stringstream ssDuration;
        ssDuration << +banDays << "d";
        std::string sDuration;
        sDuration           = ssDuration.str();
        std::string sText   = ("Player: " + std::string(Client->GetPlayerName()) + " have used Cheating software and have banned for " + sDuration.c_str());
        sWorld->SendGMText(LANG_GM_BROADCAST, sText.c_str());
        sLog->outWarden("Player %s (account: %u) have used Cheating software and have banned for %s", Client->GetPlayerName(), Client->GetAccountId(), sDuration.c_str());
        sWorld->BanAccount(BAN_CHARACTER, Client->GetPlayerName(), sDuration.c_str(), "Cheating software user", "Server guard");
    } else if(found && sWorld->getBoolConfig(CONFIG_BOOL_WARDEN_KICK))
        Client->KickPlayer();
}
