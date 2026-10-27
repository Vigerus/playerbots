#include "PlayerbotMapServer.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/StateTree/StateMachine.h"
#include "playerbot/StateTree/STAI_ActivityLog.h"

#include "Globals/ObjectAccessor.h"
#include "Server/DBCStores.h"
#include "Server/SQLStorages.h"

#include <boost/asio.hpp>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

using namespace ai;
using boost::asio::ip::tcp;

namespace
{
    // what Update writes (world thread) and the connections read (their threads), behind hub->lock
    struct Hub
    {
        std::atomic<bool> running{ false };

        std::mutex lock;
        std::shared_ptr<const std::string> players;
        std::map<uint64, uint32> asked;                                 // bot guid -> WorldTimer of its last request
        std::map<uint64, std::shared_ptr<const std::string>> bots;      // bot guid -> its latest copy
    };

    // never destroyed: connections may still run while the process shuts down
    Hub* hub = new Hub();

    constexpr uint32 playersIntervalMs = 1000;
    constexpr uint32 botsIntervalMs = 500;
    constexpr uint32 askedTimeoutMs = 10000;   // a bot nobody asked for this long is not copied any more
    constexpr size_t historyEntries = 200;

    //--------------------------------------------------------------------
    // JSON
    //--------------------------------------------------------------------

    std::string Quote(const std::string& text)
    {
        std::string out = "\"";
        for (const char c : text)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (uint8(c) < 0x20)
                    {
                        char escaped[8];
                        snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                        out += escaped;
                    }
                    else
                        out += c;
            }
        }
        return out + "\"";
    }

    std::string Number(float value)
    {
        char text[32];
        snprintf(text, sizeof(text), "%.2f", value);
        return text;
    }

    // the spells a view mentions ({spell:<id>} in texts, the spell lists), sent once with their names
    struct SpellSet
    {
        std::set<uint32> ids;

        void AddMarkup(const std::string& text)
        {
            for (size_t open = text.find("{spell:"); open != std::string::npos; open = text.find("{spell:", open + 7))
                ids.insert(uint32(strtoul(text.c_str() + open + 7, nullptr, 10)));
        }

        std::string List(const std::vector<uint32>& spells)
        {
            std::string out = "[";
            for (size_t i = 0; i < spells.size(); ++i)
            {
                ids.insert(spells[i]);
                out += (i ? "," : "") + std::to_string(spells[i]);
            }
            return out + "]";
        }

        std::string Names() const
        {
            std::string out = "{";
            bool first = true;
            for (uint32 id : ids)
            {
                std::string name = "spell " + std::to_string(id);
                std::string rank;
                if (const SpellEntry* spell = sSpellTemplate.LookupEntry<SpellEntry>(id))
                {
                    if (spell->SpellName[0] && *spell->SpellName[0])
                        name = spell->SpellName[0];
                    if (spell->Rank[0])
                        rank = spell->Rank[0];
                }
                out += (first ? "\"" : ",\"") + std::to_string(id) + "\":{\"name\":" + Quote(name) + ",\"rank\":" + Quote(rank) + "}";
                first = false;
            }
            return out + "}";
        }
    };

    std::string Tree(const StateNodeDebugTree& tree, SpellSet& spells);

    std::string Node(const StateNodeDebugChild& child, SpellSet& spells)
    {
        std::ostringstream out;
        out << "{\"name\":" << Quote(child.name)
            << ",\"active\":" << (child.active ? "true" : "false")
            << ",\"disabled\":" << (child.disabled ? "true" : "false")
            << ",\"any\":" << (child.conditionsAny ? "true" : "false")
            << ",\"currentTask\":" << child.currentTask
            << ",\"info\":" << Quote(child.info)
            << ",\"spells\":" << spells.List(child.spells);
        spells.AddMarkup(child.info);

        out << ",\"tasks\":[";
        for (size_t task = 0; task < child.tasks.size(); ++task)
        {
            const std::string info = task < child.taskInfos.size() ? child.taskInfos[task] : "";
            const bool disabled = task < child.tasksDisabled.size() && child.tasksDisabled[task];
            spells.AddMarkup(info);
            out << (task ? "," : "") << "{\"name\":" << Quote(child.tasks[task]) << ",\"info\":" << Quote(info)
                << ",\"disabled\":" << (disabled ? "true" : "false") << "}";
        }
        out << "],\"conditions\":[";
        for (size_t condition = 0; condition < child.conditions.size(); ++condition)
        {
            const int forced = condition < child.conditionOverrides.size() ? child.conditionOverrides[condition] : 0;
            out << (condition ? "," : "") << "{\"text\":" << Quote(child.conditions[condition].first)
                << ",\"result\":" << int(child.conditions[condition].second) << ",\"forced\":" << forced << "}";
        }
        out << "],\"transitions\":[";
        for (size_t i = 0; i < child.transitions.size(); ++i)
            out << (i ? "," : "") << Quote(child.transitions[i]);
        out << "],\"subtrees\":[";
        for (size_t i = 0; i < child.subtrees.size(); ++i)
            out << (i ? "," : "") << Tree(child.subtrees[i], spells);
        out << "]}";
        return out.str();
    }

    std::string Tree(const StateNodeDebugTree& tree, SpellSet& spells)
    {
        std::ostringstream out;
        out << "{\"name\":" << Quote(tree.name) << ",\"running\":" << (tree.running ? "true" : "false")
            << ",\"task\":" << tree.task << ",\"globals\":[";
        for (size_t i = 0; i < tree.globalTasks.size(); ++i)
            out << (i ? "," : "") << "{\"name\":" << Quote(tree.globalTasks[i]) << ",\"spells\":"
                << spells.List(i < tree.globalSpells.size() ? tree.globalSpells[i] : std::vector<uint32>()) << "}";
        out << "],\"nodes\":[";
        for (size_t i = 0; i < tree.nodes.size(); ++i)
            out << (i ? "," : "") << Node(tree.nodes[i], spells);
        out << "]}";
        return out.str();
    }

    // what the debug window shows of a bot: movement, states (with their tree stacks) and the activity history
    std::string BotView(Player* bot, PlayerbotAI* ai, StateMachine& stateMachine, uint32 now)
    {
        StateMachineDebugView machine;
        stateMachine.GetDebugView(machine);
        SpellSet spells;

        std::ostringstream out;
        out << "{\"guid\":" << bot->GetObjectGuid().GetRawValue() << ",\"name\":" << Quote(bot->GetName())
            << ",\"active\":" << Quote(machine.active) << ",\"msInState\":" << machine.msInState;

        PlayerbotMovement& movement = ai->GetMovement();
        const MovementGoalRequest& goal = movement.GetActive();
        const STAI_Location& destination = movement.GetDestination();
        const float distance = destination.valid && destination.mapId == bot->GetMapId() ?
            bot->GetDistance(destination.x, destination.y, destination.z) : -1.0f;
        out << ",\"movement\":{\"goal\":" << Quote(goal.name) << ",\"source\":" << Quote(goal.source)
            << ",\"state\":" << Quote(movement.GetStateName()) << ",\"priority\":" << goal.priority
            << ",\"distance\":" << Number(distance) << "}";

        out << ",\"states\":[";
        for (size_t i = 0; i < machine.states.size(); ++i)
        {
            const StateMachineDebugView::State& state = machine.states[i];
            out << (i ? "," : "") << "{\"type\":" << Quote(state.type) << ",\"name\":" << Quote(state.name)
                << ",\"running\":" << (state.running ? "true" : "false") << ",\"stack\":[";
            for (size_t depth = 0; depth < state.stack.size(); ++depth)
                out << (depth ? "," : "") << Tree(state.stack[depth], spells);
            out << "]}";
        }

        // newest first, as the window lists it; a running entry reports the time so far
        out << "],\"history\":[";
        const std::deque<STAI_ActivityLog::Entry>& entries = stateMachine.GetActivityLog().GetEntries();
        size_t written = 0;
        for (auto entry = entries.rbegin(); entry != entries.rend() && written < historyEntries; ++entry, ++written)
        {
            const uint32 age = WorldTimer::getMSTimeDiff(entry->startMs, now);
            const bool running = entry->open || WorldTimer::getMSTimeDiff(entry->startMs, entry->endMs) > age;
            const uint32 duration = running ? age : WorldTimer::getMSTimeDiff(entry->startMs, entry->endMs);
            spells.AddMarkup(entry->name);
            out << (written ? "," : "") << "{\"name\":" << Quote(entry->name) << ",\"source\":" << Quote(entry->source)
                << ",\"age\":" << age << ",\"duration\":" << duration << ",\"running\":" << (running ? "true" : "false")
                << ",\"result\":" << int(entry->result) << ",\"count\":" << entry->count
                << ",\"failures\":" << entry->failures << ",\"total\":" << (entry->doneMs + duration) << "}";
        }
        out << "],\"historySize\":" << entries.size() << ",\"spellNames\":" << spells.Names() << "}";
        return out.str();
    }

    const char* Kind(Player* player)
    {
        PlayerbotAI* ai = player->GetPlayerbotAI();
        if (!ai || ai->IsRealPlayer())
            return "human";
        return sRandomPlayerbotMgr.IsRandomBot(player) ? "random" : "playerbot";
    }

    std::string Zone(Player* player)
    {
        if (const AreaTableEntry* area = GetAreaEntryByAreaID(player->GetZoneId()))
            return area->area_name[0];
        if (const MapEntry* map = sMapStore.LookupEntry(player->GetMapId()))
            return map->name[0];
        return "";
    }

    //--------------------------------------------------------------------
    // HTTP (each connection on its own thread)
    //--------------------------------------------------------------------

    void Reply(tcp::socket& socket, const char* status, const char* type, const std::string& body)
    {
        std::ostringstream head;
        head << "HTTP/1.1 " << status << "\r\nContent-Type: " << type << "\r\nContent-Length: " << body.size()
            << "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
        boost::system::error_code error;
        boost::asio::write(socket, boost::asio::buffer(head.str()), error);
        if (!error)
            boost::asio::write(socket, boost::asio::buffer(body), error);
    }

    void Serve(std::shared_ptr<tcp::socket> socket)
    {
        // the request line is all that is read: GET <path> HTTP/1.x
        std::string request;
        char buffer[1024];
        boost::system::error_code error;
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192)
        {
            const size_t n = socket->read_some(boost::asio::buffer(buffer), error);
            if (error)
                return;
            request.append(buffer, n);
        }
        std::istringstream line(request);
        std::string method, path;
        line >> method >> path;
        if (method != "GET")
        {
            Reply(*socket, "405 Method Not Allowed", "text/plain", "GET only");
            return;
        }

        if (path == "/players")
        {
            std::shared_ptr<const std::string> players;
            {
                std::lock_guard<std::mutex> guard(hub->lock);
                players = hub->players;
            }
            Reply(*socket, "200 OK", "application/json; charset=utf-8", players ? *players : "{\"players\":[]}");
        }
        else if (path.rfind("/bot/", 0) == 0)
        {
            const uint64 guid = strtoull(path.c_str() + 5, nullptr, 10);
            std::shared_ptr<const std::string> view;
            {
                std::lock_guard<std::mutex> guard(hub->lock);
                hub->asked[guid] = WorldTimer::getMSTime();
                auto found = hub->bots.find(guid);
                if (found != hub->bots.end())
                    view = found->second;
            }
            Reply(*socket, "200 OK", "application/json; charset=utf-8", view ? *view : "{\"pending\":true}");
        }
        else if (path.rfind("/icon/", 0) == 0)
        {
            const uint32 spellId = uint32(strtoul(path.c_str() + 6, nullptr, 10));
            std::ifstream file("spellicons/" + std::to_string(spellId) + ".jpg", std::ios::binary);
            if (!spellId || !file)
            {
                Reply(*socket, "404 Not Found", "text/plain", "no icon");
                return;
            }
            const std::string image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            Reply(*socket, "200 OK", "image/jpeg", image);
        }
        else
            Reply(*socket, "404 Not Found", "text/plain", "/players, /bot/<guid>, /icon/<spell id>");
    }

    void Listen(std::string address, uint16 port)
    {
        try
        {
            boost::asio::io_context io;
            tcp::acceptor acceptor(io, tcp::endpoint(boost::asio::ip::make_address(address), port));
            sLog.outString("Playerbot map server listening on %s:%u", address.c_str(), uint32(port));
            for (;;)
            {
                auto socket = std::make_shared<tcp::socket>(io);
                acceptor.accept(*socket);
                std::thread(Serve, socket).detach();
            }
        }
        catch (std::exception& e)
        {
            sLog.outError("Playerbot map server on %s:%u stopped: %s", address.c_str(), uint32(port), e.what());
            hub->running = false;
        }
    }
}

PlayerbotMapServer& PlayerbotMapServer::instance()
{
    static PlayerbotMapServer server;
    return server;
}

void PlayerbotMapServer::Start(const std::string& address, uint16 port)
{
    if (hub->running.exchange(true))
        return;
    std::thread(Listen, address, port).detach();
}

bool PlayerbotMapServer::IsRunning() const
{
    return hub->running;
}

void PlayerbotMapServer::Update()
{
    if (!hub->running)
        return;

    static uint32 playersMs = 0, botsMs = 0;
    const uint32 now = WorldTimer::getMSTime();
    const bool playersDue = !playersMs || WorldTimer::getMSTimeDiff(playersMs, now) >= playersIntervalMs;
    const bool botsDue = !botsMs || WorldTimer::getMSTimeDiff(botsMs, now) >= botsIntervalMs;
    if (!playersDue && !botsDue)
        return;

    // the bots someone asked for lately
    std::set<uint64> asked;
    if (botsDue)
    {
        botsMs = now ? now : 1;
        std::lock_guard<std::mutex> guard(hub->lock);
        for (auto it = hub->asked.begin(); it != hub->asked.end();)
        {
            if (WorldTimer::getMSTimeDiff(it->second, now) > askedTimeoutMs)
            {
                hub->bots.erase(it->first);
                it = hub->asked.erase(it);
            }
            else
                asked.insert((it++)->first);
        }
    }
    if (!playersDue && asked.empty())
        return;

    // the players are read after the list is copied (nothing here may take the list's lock again); they cannot log
    // out meanwhile, that happens on this thread too
    std::vector<Player*> players;
    {
        HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
        for (auto& [guid, player] : sObjectAccessor.GetPlayers())
            if (player && player->IsInWorld())
                players.push_back(player);
    }

    std::ostringstream list;
    if (playersDue)
    {
        playersMs = now ? now : 1;
        list << "{\"time\":" << now << ",\"players\":[";
    }
    std::map<uint64, std::shared_ptr<const std::string>> views;
    bool first = true;
    for (Player* player : players)
    {
        const uint64 guid = player->GetObjectGuid().GetRawValue();
        if (playersDue)
        {
            list << (first ? "" : ",") << "{\"guid\":" << guid << ",\"name\":" << Quote(player->GetName())
                << ",\"race\":" << uint32(player->getRace()) << ",\"class\":" << uint32(player->getClass())
                << ",\"level\":" << player->GetLevel() << ",\"kind\":\"" << Kind(player) << "\""
                << ",\"map\":" << player->GetMapId() << ",\"x\":" << Number(player->GetPositionX())
                << ",\"y\":" << Number(player->GetPositionY()) << ",\"z\":" << Number(player->GetPositionZ())
                << ",\"zone\":" << Quote(Zone(player)) << "}";
            first = false;
        }
        if (asked.count(guid))
        {
            PlayerbotAI* ai = player->GetPlayerbotAI();
            if (ai && !ai->IsRealPlayer())
                views[guid] = std::make_shared<const std::string>(BotView(player, ai, ai->StateMachine, now));
        }
    }

    std::lock_guard<std::mutex> guard(hub->lock);
    if (playersDue)
    {
        list << "]}";
        hub->players = std::make_shared<const std::string>(list.str());
    }
    for (uint64 guid : asked)
    {
        auto view = views.find(guid);
        hub->bots[guid] = view != views.end() ? view->second : std::make_shared<const std::string>("{\"missing\":true}");
    }
}
