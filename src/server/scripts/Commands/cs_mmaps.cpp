/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
* @file cs_mmaps.cpp
* @brief .mmap related commands
*
* This file contains the CommandScripts for all
* mmap sub-commands
*/

#include "ScriptMgr.h"
#include "Chat.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PointMovementGenerator.h"
#include "PathGenerator.h"
#include "MMapFactory.h"
#include "Map.h"
#include "TargetedMovementGenerator.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"

class mmaps_commandscript : public CommandScript
{
public:
    mmaps_commandscript() : CommandScript("mmaps_commandscript") { }

    ChatCommand* GetCommands() const
    {
        static ChatCommand mmapCommandTable[] =
        {
            { "path",           SEC_ADMINISTRATOR,     false, &HandleMmapPathCommand,            "", NULL },
            { "loc",            SEC_ADMINISTRATOR,     false, &HandleMmapLocCommand,             "", NULL },
            { "loadedtiles",    SEC_ADMINISTRATOR,     false, &HandleMmapLoadedTilesCommand,     "", NULL },
            { "stats",          SEC_ADMINISTRATOR,     false, &HandleMmapStatsCommand,           "", NULL },
            { "testarea",       SEC_ADMINISTRATOR,     false, &HandleMmapTestArea,               "", NULL },
            { NULL,             0,                     false, NULL,                              "", NULL }
        };

        static ChatCommand commandTable[] =
        {
            { "mmap",           SEC_ADMINISTRATOR,     true,  NULL,                 "", mmapCommandTable  },
            { NULL,             0,                     false, NULL,                              "", NULL }
        };
        return commandTable;
    }

    static bool HandleMmapPathCommand(ChatHandler* handler, char const* args)
    {
        if (!MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(handler->GetSession()->GetPlayer()->GetMapId()))
        {
            handler->PSendSysMessage("NavMesh not loaded for current map.");
            return true;
        }

        handler->PSendSysMessage("mmap path:");

        // units
        Player* player = handler->GetSession()->GetPlayer();
        Unit* target = handler->getSelectedUnit();
        if (!player || !target)
        {
            handler->PSendSysMessage("Invalid target/source selection.");
            return true;
        }

        char* para = strtok((char*)args, " ");

        bool useStraightPath = false;
        if (para && strcmp(para, "true") == 0)
            useStraightPath = true;

        // unit locations
        float x, y, z;
        player->GetPosition(x, y, z);

        // path
        PathGenerator path(target);
        path.SetUseStraightPath(useStraightPath);
        bool result = path.CalculatePath(x, y, z);

        PointsArray pointPath = path.GetPath();
        handler->PSendSysMessage("%s's path to %s:", target->GetName().c_str(), player->GetName().c_str());
        handler->PSendSysMessage("Building: %s", useStraightPath ? "StraightPath" : "SmoothPath");
        handler->PSendSysMessage("Result: %s - Length: %i - Type: %u", (result ? "true" : "false"), pointPath.size(), path.GetPathType());

        Vector3 start = path.GetStartPosition();
        Vector3 end = path.GetEndPosition();
        Vector3 actualEnd = path.GetActualEndPosition();

        handler->PSendSysMessage("StartPosition     (%.3f, %.3f, %.3f)", start.x, start.y, start.z);
        handler->PSendSysMessage("EndPosition       (%.3f, %.3f, %.3f)", end.x, end.y, end.z);
        handler->PSendSysMessage("ActualEndPosition (%.3f, %.3f, %.3f)", actualEnd.x, actualEnd.y, actualEnd.z);

        if (!player->isGameMaster())
            handler->PSendSysMessage("Enable GM mode to see the path points.");

        for (uint32 i = 0; i < pointPath.size(); ++i)
            player->SummonCreature(VISUAL_WAYPOINT, pointPath[i].x, pointPath[i].y, pointPath[i].z, 0, TEMPSUMMON_TIMED_DESPAWN, 9000);

        return true;
    }

    static bool HandleMmapLocCommand(ChatHandler* handler, char const* /*args*/)
    {
        handler->PSendSysMessage("mmap tileloc:");

        // grid tile location
        Player* player = handler->GetSession()->GetPlayer();

        int32 gx = 32 - player->GetPositionX() / SIZE_OF_GRIDS;
        int32 gy = 32 - player->GetPositionY() / SIZE_OF_GRIDS;

        handler->PSendSysMessage("%03u%02i%02i.mmtile", player->GetMapId(), gy, gx);
        handler->PSendSysMessage("gridloc [%i,%i]", gx, gy);

        // calculate navmesh tile location
        dtNavMesh const* navmesh = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(handler->GetSession()->GetPlayer()->GetMapId());
        dtNavMeshQuery const* navmeshquery = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(handler->GetSession()->GetPlayer()->GetMapId(), player->GetInstanceId());
        if (!navmesh || !navmeshquery)
        {
            handler->PSendSysMessage("NavMesh not loaded for current map.");
            return true;
        }

        float const* min = navmesh->getParams()->orig;
        float x, y, z;
        player->GetPosition(x, y, z);
        float location[VERTEX_SIZE] = {y, z, x};
        float extents[VERTEX_SIZE] = {3.0f, 5.0f, 3.0f};

        int32 tilex = int32((y - min[0]) / SIZE_OF_GRIDS);
        int32 tiley = int32((x - min[2]) / SIZE_OF_GRIDS);

        handler->PSendSysMessage("Calc   [%02i,%02i]", tilex, tiley);

        // navmesh poly -> navmesh tile location
        dtQueryFilter filter = dtQueryFilter();
        dtPolyRef polyRef = INVALID_POLYREF;
        navmeshquery->findNearestPoly(location, extents, &filter, &polyRef, NULL);

        if (polyRef == INVALID_POLYREF)
            handler->PSendSysMessage("Dt     [??,??] (invalid poly, probably no tile loaded)");
        else
        {
            dtMeshTile const* tile;
            dtPoly const* poly;
            navmesh->getTileAndPolyByRef(polyRef, &tile, &poly);
            if (tile)
                handler->PSendSysMessage("Dt     [%02i,%02i]", tile->header->x, tile->header->y);
            else
                handler->PSendSysMessage("Dt     [??,??] (no tile loaded)");
        }

        return true;
    }

    static bool HandleMmapLoadedTilesCommand(ChatHandler* handler, char const* /*args*/)
    {
        uint32 mapid = handler->GetSession()->GetPlayer()->GetMapId();
        dtNavMesh const* navmesh = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(mapid);
        dtNavMeshQuery const* navmeshquery = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(mapid, handler->GetSession()->GetPlayer()->GetInstanceId());
        if (!navmesh || !navmeshquery)
        {
            handler->PSendSysMessage("NavMesh not loaded for current map.");
            return true;
        }

        handler->PSendSysMessage("mmap loadedtiles:");

        for (int32 i = 0; i < navmesh->getMaxTiles(); ++i)
        {
            dtMeshTile const* tile = navmesh->getTile(i);
            if (!tile || !tile->header)
                continue;

            handler->PSendSysMessage("[%02i,%02i]", tile->header->x, tile->header->y);
        }

        return true;
    }

    static bool HandleMmapStatsCommand(ChatHandler* handler, char const* /*args*/)
    {
        uint32 mapId = handler->GetSession()->GetPlayer()->GetMapId();
        handler->PSendSysMessage("mmap stats:");
        handler->PSendSysMessage("  global mmap pathfinding is %sabled", MMAP::MMapFactory::IsPathfindingEnabled(mapId) ? "en" : "dis");

        MMAP::MMapManager* manager = MMAP::MMapFactory::createOrGetMMapManager();
        handler->PSendSysMessage(" %u maps loaded with %u tiles overall", manager->getLoadedMapsCount(), manager->getLoadedTilesCount());

        dtNavMesh const* navmesh = manager->GetNavMesh(handler->GetSession()->GetPlayer()->GetMapId());
        if (!navmesh)
        {
            handler->PSendSysMessage("NavMesh not loaded for current map.");
            return true;
        }

        uint32 tileCount = 0;
        uint32 nodeCount = 0;
        uint32 polyCount = 0;
        uint32 vertCount = 0;
        uint32 triCount = 0;
        uint32 triVertCount = 0;
        uint32 dataSize = 0;
        for (int32 i = 0; i < navmesh->getMaxTiles(); ++i)
        {
            dtMeshTile const* tile = navmesh->getTile(i);
            if (!tile || !tile->header)
                continue;

            tileCount++;
            nodeCount += tile->header->bvNodeCount;
            polyCount += tile->header->polyCount;
            vertCount += tile->header->vertCount;
            triCount += tile->header->detailTriCount;
            triVertCount += tile->header->detailVertCount;
            dataSize += tile->dataSize;
        }

        handler->PSendSysMessage("Navmesh stats:");
        handler->PSendSysMessage(" %u tiles loaded", tileCount);
        handler->PSendSysMessage(" %u BVTree nodes", nodeCount);
        handler->PSendSysMessage(" %u polygons (%u vertices)", polyCount, vertCount);
        handler->PSendSysMessage(" %u triangles (%u vertices)", triCount, triVertCount);
        handler->PSendSysMessage(" %.2f MB of data (not including pointers)", ((float)dataSize / sizeof(unsigned char)) / 1048576);

        return true;
    }

    static bool HandleMmapTestArea(ChatHandler* handler, char const* /*args*/)
    {
        float radius = 40.0f;
        WorldObject* object = handler->GetSession()->GetPlayer();

        CellCoord pair(Trinity::ComputeCellCoord(object->GetPositionX(), object->GetPositionY()));
        Cell cell(pair);
        cell.SetNoCreate();

        std::list<Creature*> creatureList;

        Trinity::AnyUnitInObjectRangeCheck go_check(object, radius);
        Trinity::CreatureListSearcher<Trinity::AnyUnitInObjectRangeCheck> go_search(object, creatureList, go_check);
        TypeContainerVisitor<Trinity::CreatureListSearcher<Trinity::AnyUnitInObjectRangeCheck>, GridTypeMapContainer> go_visit(go_search);

        // Get Creatures
        cell.Visit(pair, go_visit, *(object->GetMap()), *object, radius);

        if (!creatureList.empty())
        {
            handler->PSendSysMessage("Found %i Creatures.", creatureList.size());

            uint32 paths = 0;
            uint32 uStartTime = getMSTime();

            float gx, gy, gz;
            object->GetPosition(gx, gy, gz);
            for (std::list<Creature*>::iterator itr = creatureList.begin(); itr != creatureList.end(); ++itr)
            {
                PathGenerator path(*itr);
                path.CalculatePath(gx, gy, gz);
                ++paths;
            }

            uint32 uPathLoadTime = getMSTimeDiff(uStartTime, getMSTime());
            handler->PSendSysMessage("Generated %i paths in %i ms", paths, uPathLoadTime);
        }
        else
            handler->PSendSysMessage("No creatures in %f yard range.", radius);

        return true;
    }
};

void AddSC_mmaps_commandscript()
{
    new mmaps_commandscript();
}
