#pragma once

#include <boost/functional/hash.hpp>
#include "GuidPosition.h"
#include "WorldSquare.h"

#include <tuple>
#include <unordered_map>
#include <vector>

namespace ai
{
	class MapTransfer
	{
	public:
		MapTransfer(const WorldPosition& pointFrom, const WorldPosition& pointTo, float portalLength = 0.1f)
			: pointFrom(pointFrom), pointTo(pointTo), portalLength(portalLength) {
		}
		float Distance(const WorldPosition& start, const WorldPosition& end) const { return (isUseful(start, end) ? (start.distance(pointFrom) + portalLength + pointTo.distance(end)) : 200000); }

		float FDist(const WorldPosition& start, const WorldPosition& end) const { return start.fDist(pointFrom) + portalLength + pointTo.fDist(end); }

		float sqDist(const WorldPosition& start, const WorldPosition& end) const { return start.sqDistance2d(pointFrom) + (portalLength * portalLength) + pointTo.sqDistance2d(end); }

		WorldPosition GetPointFrom() const { return pointFrom; }
		WorldPosition GetPointTo() const { return pointTo; }
	private:
		bool isFrom(const WorldPosition& point) const { return point.getMapId() == pointFrom.getMapId(); }
		bool isTo(const WorldPosition& point) const { return point.getMapId() == pointTo.getMapId(); }

		bool isUseful(const WorldPosition& point) const { return isFrom(point) || isTo(point); }
		bool isUseful(const WorldPosition& start, const WorldPosition& end) const { return isFrom(start) && isTo(end); }

		float Distance(const WorldPosition& point) const { return isUseful(point) ? (isFrom(point) ? point.distance(pointFrom) : point.distance(pointTo)) : 200000; }

		WorldPosition pointFrom, pointTo;
		float portalLength = 0.1f;
	};

	// Geography for the bots' travel: map transfers (portals, boats, zeppelins from the travel node graph), area
	// levels, mmap tiles known to be bad and the mob avoid areas of the pathfinder. The quest / rpg / grind travel
	// destinations and the bots' travel targets of the legacy TravelMgr are in deprecated/playerbot/TravelMgr.*.
	class TravelMgr
	{
	public:
		TravelMgr() {};

		void SetMobAvoidArea();

		void LoadMapTransfers();
		std::vector<std::pair<WorldPosition, float>> sqMapTransDistances(const WorldPosition& start, uint32 endMapId) const;
		float MapTransDistance(const WorldPosition& start, const WorldPosition& end, bool toMap = false) const;

		void AddBadMmap(uint32 mapId, int x, int y) { badMmap.push_back(std::make_tuple(mapId, x, y)); }
		bool IsBadMmap(uint32 mapId, int x, int y) const { return std::find(badMmap.begin(), badMmap.end(), std::make_tuple(mapId, x, y)) != badMmap.end(); }

		int32 GetAreaLevel(uint32 area_id);
		void LoadAreaLevels();
	private:
		void SetMobAvoidAreaMap(uint32 mapId);

		void AddMapTransfer(WorldPosition start, WorldPosition end, float portalDistance = 0.1f, bool makeShortcuts = true);

		std::unordered_map<uint32, int32> areaLevels;

		std::vector<std::tuple<uint32, int, int>> badMmap;

		std::unordered_map<std::pair<uint32, uint32>, std::vector<MapTransfer>, boost::hash<std::pair<uint32, uint32>>> mapTransfersMap;
	};
}

#define sTravelMgr MaNGOS::Singleton<TravelMgr>::Instance()
