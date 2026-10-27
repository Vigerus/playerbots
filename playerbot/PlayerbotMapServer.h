#pragma once

#include "Platform/Define.h"

#include <string>

namespace ai
{
    // Read-only HTTP / JSON endpoint for the PlayerbotsAndroid map app (AiPlayerbot.MapServerPort, off with 0):
    //   GET /players     every online character: guid, name, race, class, level, kind (human / random / playerbot),
    //                    map, x / y / z, zone
    //   GET /bot/<guid>  a bot's state machine, movement and activity history: what the server's debug window shows
    //                    (PlayerbotDebugWindow); a bot is copied only while it is asked for, {"pending":true} until
    //                    the first copy, {"missing":true} once it is not online
    //   GET /icon/<id>   a spell icon from the debug window's spellicons/ cache (404 when it has none)
    //
    // Nothing is read on the server's thread: Update (the world thread, while no map updates) copies the data into
    // JSON documents, the server's threads only send the latest ones. Nothing can be changed through it.
    class PlayerbotMapServer
    {
    public:
        static PlayerbotMapServer& instance();

        // starts listening (address: the interface, 0.0.0.0 every one); no-op when it runs
        void Start(const std::string& address, uint16 port);
        bool IsRunning() const;

        // from World::Update, outside the map updates
        void Update();
    };
}

#define sPlayerbotMapServer ai::PlayerbotMapServer::instance()
