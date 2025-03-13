#pragma once
#include <vector>
#include <flecs.h>

namespace dmaps
{
  void gen_player_approach_map(flecs::world &ecs, std::vector<float> &map);
  void gen_player_flee_map(flecs::world &ecs, std::vector<float> &map);
  void gen_hive_pack_map(flecs::world &ecs, std::vector<float> &map);

  void gen_character_approach_map(flecs::world &ecs, std::vector<float> &map, size_t characterId);
  void gen_specific_approach_map(flecs::world &ecs, std::vector<float> &map, const int &targetPosX, const int &targetPosY);
};

