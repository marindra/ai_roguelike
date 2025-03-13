#include "dijkstraMapGen.h"
#include "ecsTypes.h"
#include "dungeonUtils.h"

template<typename Callable>
static void query_dungeon_data(flecs::world &ecs, Callable c)
{
  static auto dungeonDataQuery = ecs.query<const DungeonData>();

  dungeonDataQuery.each(c);
}

template<typename Callable>
static void query_characters_positions(flecs::world &ecs, Callable c)
{
  static auto characterPositionQuery = ecs.query<const Position, const Team>();

  characterPositionQuery.each(c);
}

template<typename Callable>
static void query_certain_character_position(flecs::world &ecs, Callable c)
{
  static auto characterPositionQuery = ecs.query<const Position, const CharacterID>();

  characterPositionQuery.each(c);
}

constexpr float invalid_tile_value = 1e5f;

static void init_tiles(std::vector<float> &map, const DungeonData &dd)
{
  map.resize(dd.width * dd.height);
  for (float &v : map)
    v = invalid_tile_value;
}

// scan version, could be implemented as Dijkstra version as well
static void process_dmap(std::vector<float> &map, const DungeonData &dd, const bool processItems = false)
{
  bool done = false;
  auto getMapAt = [&](size_t x, size_t y, float def, bool allowedItem=false)
  {
    if (x < dd.width && y < dd.width && dd.tiles[y * dd.width + x] == dungeon::floor)
      return map[y * dd.width + x];
    if (allowedItem && x < dd.width && y < dd.width && dd.tiles[y * dd.width + x] == dungeon::item)
      return map[y * dd.width + x];
    return def;
  };
  auto getMinNei = [&](size_t x, size_t y)
  {
    float val = map[y * dd.width + x];
    val = std::min(val, getMapAt(x - 1, y + 0, val));
    val = std::min(val, getMapAt(x + 1, y + 0, val));
    val = std::min(val, getMapAt(x + 0, y - 1, val));
    val = std::min(val, getMapAt(x + 0, y + 1, val));
    return val;
  };
  while (!done)
  {
    done = true;
    for (size_t y = 0; y < dd.height; ++y)
      for (size_t x = 0; x < dd.width; ++x)
      {
        const size_t i = y * dd.width + x;
        if (dd.tiles[i] != dungeon::floor)
          continue;
        const float myVal = getMapAt(x, y, invalid_tile_value);
        const float minVal = getMinNei(x, y);
        if (minVal < myVal - 1.f)
        {
          map[i] = minVal + 1.f;
          done = false;
        }
      }
  }

  if (!processItems)
    return;
  // set values to the items!
  for (size_t y = 0; y < dd.height; ++y)
    for (size_t x = 0; x < dd.width; ++x)
    {
      const size_t i = y * dd.width + x;
      if (dd.tiles[i] != dungeon::item)
        continue;
      const float myVal = getMapAt(x, y, invalid_tile_value, true);
      const float minVal = getMinNei(x, y);
      if (minVal < myVal - 1.f)
      {
        map[i] = minVal + 1.f;
      }
    }
}

void dmaps::gen_player_approach_map(flecs::world &ecs, std::vector<float> &map)
{
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    query_characters_positions(ecs, [&](const Position &pos, const Team &t)
    {
      if (t.team == 0) // player team hardcode
        map[pos.y * dd.width + pos.x] = 0.f;
    });
    process_dmap(map, dd);
  });
}

void dmaps::gen_player_flee_map(flecs::world &ecs, std::vector<float> &map)
{
  gen_player_approach_map(ecs, map);
  for (float &v : map)
    if (v < invalid_tile_value)
      v *= -1.2f;
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    process_dmap(map, dd);
  });
}

void dmaps::gen_hive_pack_map(flecs::world &ecs, std::vector<float> &map)
{
  static auto hiveQuery = ecs.query<const Position, const Hive>();
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    hiveQuery.each([&](const Position &pos, const Hive &)
    {
      map[pos.y * dd.width + pos.x] = 0.f;
    });
    process_dmap(map, dd);
  });
}

void dmaps::gen_character_approach_map(flecs::world &ecs, std::vector<float> &map, size_t characterId)
{
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    Position tmpPos;
    init_tiles(map, dd);
    query_certain_character_position(ecs, [&](const Position &pos, const CharacterID &charId)
    {
      if (charId.id == characterId)
      {
        map[pos.y * dd.width + pos.x] = 0.f;
        tmpPos = pos;
      }
    });
    process_dmap(map, dd, true);
  });
}

void dmaps::gen_specific_approach_map(flecs::world &ecs, std::vector<float> &map, const int &targetPosX, const int &targetPosY)
{
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    if (targetPosX < 0 || targetPosY < 0 || targetPosX >= dd.width || targetPosY >= dd.height)
      return;
    //if (dd.tiles[targetPosY * dd.width + targetPosX] == dungeon::floor)
    map[targetPosY * dd.width + targetPosX] = 0.f;

    // set values to neighbour floor tiles
    for (int i = -1; i <= 1; i += 1)
      for (int j = -1; j <= 1; j += 1)
      {
        if (i == j || i + j == 0)
          continue;
        if (targetPosY + j < 0 || targetPosY + j >= dd.height || targetPosX + i < 0
            || targetPosX + i >= dd.width)
          continue;
        if (dd.tiles[(targetPosY + j) * dd.width + targetPosX + i] == dungeon::floor)
        {
          map[(targetPosY + j) * dd.width + targetPosX + i] = 1.0f;
        }
      }
    process_dmap(map, dd);
  });
}