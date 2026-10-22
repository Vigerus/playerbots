#pragma once

#include "Entities/ObjectGuid.h"
#include "Server/WorldPacket.h"

class Player;

namespace ai
{
    class Event
	{
	public:
        Event(Event const& other)
        {
            source = other.source;
            param = other.param;
            packet = other.packet;
            owner = other.owner;
        }
        Event() {}
        Event(const std::string& source) : source(source) {}
        Event(const std::string& source, const std::string& param, Player* owner = NULL) : source(source), param(param), owner(owner) {}
        Event(const std::string& source, const WorldPacket& packet, Player* owner = NULL) : source(source), packet(packet), owner(owner) {}
        Event(const std::string& source, ObjectGuid object, Player* owner = NULL) : source(source), owner(owner) { packet << object; }
        virtual ~Event() {}

	public:
        const std::string& getSource() const { return source; }
        const std::string& getParam() const { return param; }
        WorldPacket& getPacket() { return packet; }
        ObjectGuid getObject() const;
        Player* getOwner() const { return owner; }
        bool IsValid() const { return !source.empty(); }

    protected:
        std::string source;
        std::string param;
        WorldPacket packet;
        Player* owner = nullptr;
	};
}
