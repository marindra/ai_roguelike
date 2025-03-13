#include "roguelike.h"
#include "ecsTypes.h"
#include "raylib.h"
#include "stateMachine.h"
#include "aiLibrary.h"
#include "blackboard.h"
#include "math.h"
#include "dungeonUtils.h"
#include "dijkstraMapGen.h"
#include "dmapFollower.h"

// it's necessary to shuffle items in houses
#include <chrono>
#include <algorithm>
#include <random>

static flecs::entity create_player_approacher(flecs::entity e)
{
  e.set(DmapWeights{{{"approach_map", {1.f, 1.f}}}});
  return e;
}

static flecs::entity create_player_fleer(flecs::entity e)
{
  e.set(DmapWeights{{{"flee_map", {1.f, 1.f}}}});
  return e;
}

static flecs::entity create_hive_follower(flecs::entity e)
{
  e.set(DmapWeights{{{"hive_map", {1.f, 1.f}}}});
  return e;
}

static flecs::entity create_hive_monster(flecs::entity e)
{
  e.set(DmapWeights{{{"hive_map", {1.f, 1.f}}, {"approach_map", {1.8, 0.8f}}}});
  return e;
}

static flecs::entity create_hive(flecs::entity e)
{
  e.add<Hive>();
  return e;
}


static void create_fuzzy_monster_beh(flecs::entity e)
{
  e.set(Blackboard{});
  BehNode *root =
    utility_selector({
      std::make_pair(
        sequence({
          find_enemy(e, 4.f, "flee_enemy"),
          flee(e, "flee_enemy")
        }),
        [](Blackboard &bb)
        {
          const float hp = bb.get<float>("hp");
          const float enemyDist = bb.get<float>("enemyDist");
          return (100.f - hp) * 5.f - 50.f * enemyDist;
        }
      ),
      std::make_pair(
        sequence({
          find_enemy(e, 3.f, "attack_enemy"),
          move_to_entity(e, "attack_enemy")
        }),
        [](Blackboard &bb)
        {
          const float enemyDist = bb.get<float>("enemyDist");
          return 100.f - 10.f * enemyDist;
        }
      ),
      std::make_pair(
        patrol(e, 2.f, "patrol_pos"),
        [](Blackboard &)
        {
          return 50.f;
        }
      ),
      std::make_pair(
        patch_up(100.f),
        [](Blackboard &bb)
        {
          const float hp = bb.get<float>("hp");
          return 140.f - hp;
        }
      )
    });
  e.add<WorldInfoGatherer>();
  e.set(BehaviourTree{root});
}

static void create_minotaur_beh(flecs::entity e)
{
  e.set(Blackboard{});
  BehNode *root =
    selector({
      sequence({
        is_low_hp(50.f),
        find_enemy(e, 4.f, "flee_enemy"),
        flee(e, "flee_enemy")
      }),
      sequence({
        find_enemy(e, 3.f, "attack_enemy"),
        move_to_entity(e, "attack_enemy")
      }),
      patrol(e, 2.f, "patrol_pos")
    });
  e.set(BehaviourTree{root});
}

static void create_character_beh(flecs::entity e)
{
  e.set(Blackboard{});
  BehNode *highLevelRoot = 
    utility_selector_with_priority({
      std::make_pair(
        // BehNode*
        utility_selector_with_small_random({
          /*std::make_pair(
            sequence({
              show_msg("I try to find food!"),
              find_item_with_hunger(e, "nearest_food"),
              move_to_entity_with_map(e, "nearest_food"),
              increaseSmth(e, "nearest_food", Action{EA_INC_HUNGER}, "hunger")
            }),
            [](Blackboard &bb)
            {
              const float foodDist = bb.get<float>("nearest_hunger_item_dist");
              const float hunger = bb.get<float>("hunger");
              return (100.f - hunger) * 5.f - 50.f * foodDist;
            }),*/
          std::make_pair(
            sequence({
              find_named_item(e, "hunger_item", "oven"),
              move_to_entity_with_map(e, "hunger_item"),
              increase_smth(e, "hunger_item", Action{EA_INC_HUNGER}, "hunger")
            }),
            [](Blackboard &bb)
            {
              const float foodDist = bb.get<float>("oven");
              return 100.f - foodDist;
            }
          ),
          std::make_pair(
            sequence({
              find_named_item(e, "hunger_item", "stove"),
              move_to_entity_with_map(e, "hunger_item"),
              increase_smth(e, "hunger_item", Action{EA_INC_HUNGER}, "hunger")
            }),
            [](Blackboard &bb)
            {
              const float foodDist = bb.get<float>("stove");
              return 100.f - foodDist;
            }
          ),
          std::make_pair(
            sequence({
              find_named_item(e, "hunger_item", "freezer"),
              move_to_entity_with_map(e, "hunger_item"),
              increase_smth(e, "hunger_item", Action{EA_INC_HUNGER}, "hunger")
            }),
            [](Blackboard &bb)
            {
              const float foodDist = bb.get<float>("freezer");
              return 100.f - foodDist;
            }
          ),
          std::make_pair(
            sequence({
              find_named_item(e, "hunger_item", "sink"),
              move_to_entity_with_map(e, "hunger_item"),
              increase_smth(e, "hunger_item", Action{EA_INC_HUNGER}, "sink")
            }),
            [](Blackboard &bb)
            {
              const float foodDist = bb.get<float>("sink");
              return 100.f - foodDist;
            }
          )
        }),
        std::make_pair(
          //utility function
          [](Blackboard &bb)
          {
            //const float foodDist = bb.get<float>("nearest_hunger_item_dist");
            const float hunger = bb.get<float>("hunger");
            //return (100.f - hunger) * 5.f - 50.f * foodDist;
            return 100.0f - 0.01f * hunger * hunger;
          },
          // priority
          [](Blackboard &bb)
          {
            const float hunger = bb.get<float>("hunger");
            return (hunger <= 10) * 2 + (hunger < 35 && hunger > 10) * 1;
          }
        )
      ),
      std::make_pair(
        utility_selector_with_small_random({
          std::make_pair(
            sequence({
              //show_msg("I try to find comfort!"),
              find_named_item(e, "comfort_item", "armchair"),
              move_to_entity_with_map(e, "comfort_item"),
              increase_smth(e, "comfort_item", Action{EA_INC_COMFORT}, "comfort")
            }),
            [](Blackboard &bb)
            {
              const float comfortDist = bb.get<float>("armchair");
              return (100.f - comfortDist);
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find comfort!"),
              find_named_item(e, "comfort_item", "bed"),
              move_to_entity_with_map(e, "comfort_item"),
              increase_smth(e, "comfort_item", Action{EA_INC_COMFORT}, "comfort")
            }),
            [](Blackboard &bb)
            {
              const float comfortDist = bb.get<float>("bed");
              return (100.f - comfortDist);
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find comfort!"),
              find_named_item(e, "comfort_item", "bath"),
              move_to_entity_with_map(e, "comfort_item"),
              increase_smth(e, "comfort_item", Action{EA_INC_COMFORT}, "comfort")
            }),
            [](Blackboard &bb)
            {
              const float comfortDist = bb.get<float>("bath");
              return (100.f - comfortDist);
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find comfort!"),
              find_named_item(e, "comfort_item", "chair"),
              move_to_entity_with_map(e, "comfort_item"),
              increase_smth(e, "comfort_item", Action{EA_INC_COMFORT}, "comfort")
            }),
            [](Blackboard &bb)
            {
              const float comfortDist = bb.get<float>("chair");
              return (100.f - comfortDist);
            }
          ),
        }),
        std::make_pair(
          //utility function
          [](Blackboard &bb)
          {
            //const float comfortDist = bb.get<float>("nearest_comfort_item_dist");
            const float comfort = bb.get<float>("comfort");
            return (100.f - 0.01f * comfort * comfort);// * 5.f - 50.f * comfortDist;
          },
          // priority
          [](Blackboard &bb)
          {
            const float comfort = bb.get<float>("comfort");
            return (comfort <= 40) * 1;
          }
        )
      ),
      std::make_pair(
        utility_selector_with_small_random({
          std::make_pair(
            sequence({
              //show_msg("I try to find social!"),
              find_named_item(e, "social_item", "phone"),
              move_to_entity_with_map(e, "social_item"),
              increase_smth(e, "social_item", Action{EA_INC_SOCIAL}, "social")
            }),
            [](Blackboard &bb)
            {
              const float comfortDist = bb.get<float>("phone");
              return 100.0f - comfortDist;
            }
          ),
          std::make_pair(
            sequence({
              find_character(e, "friend"),
              move_to_pos(e, "friend"),
              increase_social_once(e, 20.0f),
              find_home(e, "home"),
              //show_msg("go to home"),
              move_to_pos(e, "home"),
              //show_msg("returned")
            }),
            [](Blackboard &bb)
            {
              const int inVisit = bb.get<int>("visit");
              const float social = bb.get<float>("social");
              return 90.0f * inVisit + social;
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find social!"),
              find_named_item(e, "social_item", "computer"),
              move_to_entity_with_map(e, "social_item"),
              increase_smth(e, "social_item", Action{EA_INC_SOCIAL}, "social")
            }),
            [](Blackboard &bb)
            {
              const float socialDist = bb.get<float>("computer");
              return 100 - socialDist;
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find social!"),
              find_named_item(e, "social_item", "mirror"),
              move_to_entity_with_map(e, "social_item"),
              increase_smth(e, "social_item", Action{EA_INC_SOCIAL}, "social")
            }),
            [](Blackboard &bb)
            {
              const float socialDist = bb.get<float>("mirror");
              return 100 - socialDist;
            }
          )
        }),        
        std::make_pair(
          //utility function
          [](Blackboard &bb)
          {
            //const float socialDist = bb.get<float>("nearest_social_item_dist");
            const float social = bb.get<float>("social");
            //return (100.f - social) * 5.f - 50.f * socialDist;
            return 100.0f - 0.01f * social * social;
          },
          // priority
          [](Blackboard &bb)
          {
            const float social = bb.get<float>("social");
            const bool visitSmb = bb.get<int>("visit");
            return (social <= 40) * 1 + 1 * visitSmb;
          }
        )
      ),
      std::make_pair(
        utility_selector_with_small_random({
          std::make_pair(
            sequence({
              //show_msg("I try to find hygiene!"),
              find_named_item(e, "hygiene_item", "sink"),
              move_to_entity_with_map(e, "hygiene_item"),
              increase_smth(e, "hygiene_item", Action{EA_INC_HYGIENE}, "hygiene")
            }),
            [](Blackboard &bb)
            {
              const float hygieneDist = bb.get<float>("sink");
              return 100.0f - hygieneDist;
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find hygiene!"),
              find_named_item(e, "hygiene_item", "bath"),
              move_to_entity_with_map(e, "hygiene_item"),
              increase_smth(e, "hygiene_item", Action{EA_INC_HYGIENE}, "hygiene")
            }),
            [](Blackboard &bb)
            {
              const float hygieneDist = bb.get<float>("bath");
              return 100.0f - hygieneDist;
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find hygiene!"),
              find_named_item(e, "hygiene_item", "mirror"),
              move_to_entity_with_map(e, "hygiene_item"),
              increase_smth(e, "hygiene_item", Action{EA_INC_HYGIENE}, "hygiene")
            }),
            [](Blackboard &bb)
            {
              const float hygieneDist = bb.get<float>("mirror");
              return 100.0f - hygieneDist;
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find hygiene!"),
              find_named_item(e, "hygiene_item", "shower"),
              move_to_entity_with_map(e, "hygiene_item"),
              increase_smth(e, "hygiene_item", Action{EA_INC_HYGIENE}, "hygiene")
            }),
            [](Blackboard &bb)
            {
              const float hygieneDist = bb.get<float>("shower");
              return 100.0f - hygieneDist;
            }
          )
        }),        
        std::make_pair(
          //utility function
          [](Blackboard &bb)
          {
            //const float hygieneDist = bb.get<float>("nearest_hygiene_item_dist");
            const float hygiene = bb.get<float>("hygiene");
            //return (100.f - hygiene) * 5.f - 50.f * hygieneDist;
            return 100.0f - 0.01f * hygiene * hygiene;
          },
          // priority
          [](Blackboard &bb)
          {
            const float hygiene = bb.get<float>("hygiene");
            return (hygiene <= 10) * 2 + (hygiene < 35 && hygiene > 10) * 1;
          }
        )
      ),
      std::make_pair(
        utility_selector_with_small_random({
          std::make_pair(
            sequence({
              //show_msg("I try to find fun!"),
              find_named_item(e, "fun_item", "library"),
              move_to_entity_with_map(e, "fun_item"),
              increase_smth(e, "fun_item", Action{EA_INC_FUN}, "fun")
            }),
            [](Blackboard &bb)
            {
              const float funDist = bb.get<float>("library");
              return 100.0f - funDist;
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find fun!"),
              find_named_item(e, "fun_item", "phone"),
              move_to_entity_with_map(e, "fun_item"),
              increase_smth(e, "fun_item", Action{EA_INC_FUN}, "fun")
            }),
            [](Blackboard &bb)
            {
              const float funDist = bb.get<float>("phone");
              return 100.0f - funDist;
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find fun!"),
              find_named_item(e, "fun_item", "computer"),
              move_to_entity_with_map(e, "fun_item"),
              increase_smth(e, "fun_item", Action{EA_INC_FUN}, "fun")
            }),
            [](Blackboard &bb)
            {
              const float funDist = bb.get<float>("computer");
              return 100.0f - funDist;
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find fun!"),
              find_named_item(e, "fun_item", "chess"),
              move_to_entity_with_map(e, "fun_item"),
              increase_smth(e, "fun_item", Action{EA_INC_FUN}, "fun")
            }),
            [](Blackboard &bb)
            {
              const float funDist = bb.get<float>("chess");
              return 100.0f - funDist;
            }
          )
        }),        
        std::make_pair(
          //utility function
          [](Blackboard &bb)
          {
            //const float funDist = bb.get<float>("nearest_fun_item_dist");
            const float fun = bb.get<float>("fun");
            //return (100.f - fun) * 5.f - 50.f * funDist;
            return 100.0f - 0.01f * fun * fun;
          },
          // priority
          [](Blackboard &bb)
          {
            const float fun = bb.get<float>("fun");
            return (fun <= 40) * 1;
          }
        )
      ),
      std::make_pair(
        utility_selector_with_small_random({
          std::make_pair(
            sequence({
              //show_msg("I try to find energy!"),
              find_named_item(e, "energy_item", "coffeemaker"),
              move_to_entity_with_map(e, "energy_item"),
              increase_smth(e, "energy_item", Action{EA_INC_ENERGY}, "energy")
            }),
            [](Blackboard &bb)
            {
              const float energyDist = bb.get<float>("coffeemaker");
              return 100.0f - energyDist;
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find energy!"),
              find_named_item(e, "energy_item", "bed"),
              move_to_entity_with_map(e, "energy_item"),
              increase_smth(e, "energy_item", Action{EA_INC_ENERGY}, "energy")
            }),
            [](Blackboard &bb)
            {
              const float energyDist = bb.get<float>("bed");
              return 100.0f - energyDist;
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find energy!"),
              find_named_item(e, "energy_item", "energy-drink"),
              move_to_entity_with_map(e, "energy_item"),
              increase_smth(e, "energy_item", Action{EA_INC_ENERGY}, "energy")
            }),
            [](Blackboard &bb)
            {
              const float energyDist = bb.get<float>("energy-drink");
              return 100.0f - energyDist;
            }
          ),
          std::make_pair(
            sequence({
              //show_msg("I try to find energy!"),
              find_named_item(e, "energy_item", "armchair"),
              move_to_entity_with_map(e, "energy_item"),
              increase_smth(e, "energy_item", Action{EA_INC_ENERGY}, "energy")
            }),
            [](Blackboard &bb)
            {
              const float energyDist = bb.get<float>("armchair");
              return 100.0f - energyDist;
            }
          )
        }),
        std::make_pair(
          //utility function
          [](Blackboard &bb)
          {
            //const float energyDist = bb.get<float>("nearest_energy_item_dist");
            const float energy = bb.get<float>("energy");
            //return (100.f - energy) * 5.f - 50.f * energyDist;
            return 100.0f - 0.01f * energy * energy;
          },
          // priority
          [](Blackboard &bb)
          {
            const float energy = bb.get<float>("energy");
            return (energy <= 10) * 2 + (energy < 35 && energy > 10) * 1;
          }
        )
      ),
      std::make_pair(
        utility_selector_with_small_random({
          std::make_pair(
            sequence({
              //show_msg("I try to find bladder!"),
              find_named_item(e, "bladder_item", "WC"),
              move_to_entity_with_map(e, "bladder_item"),
              increase_smth(e, "bladder_item", Action{EA_INC_BLADDER}, "bladder")
            }),
            [](Blackboard &bb)
            {
              return 100.0f;
            }
          )
        }),
        std::make_pair(
          //utility function
          [](Blackboard &bb)
          {
            //const float bladderDist = bb.get<float>("nearest_bladder_item_dist");
            const float bladder = bb.get<float>("bladder");
            //return (100.f - bladder) * 5.f - 50.f * bladderDist;
            return 100.0f - 0.01f * bladder * bladder;
          },
          // priority
          [](Blackboard &bb)
          {
            const float bladder = bb.get<float>("bladder");
            return (bladder <= 10) * 2 + (bladder < 35 && bladder > 10) * 1;
          }
        )
      )
    });
e.add<WorldInfoGatherer>();
e.set(BehaviourTree{highLevelRoot});
}

static Position find_free_dungeon_tile(flecs::world &ecs)
{
  static auto findMonstersQuery = ecs.query<const Position, const Hitpoints>();
  bool done = false;
  while (!done)
  {
    done = true;
    Position pos = dungeon::find_walkable_tile(ecs);
    findMonstersQuery.each([&](const Position &p, const Hitpoints&)
    {
      if (p == pos)
        done = false;
    });
    if (done)
      return pos;
  };
  return {0, 0};
}

static flecs::entity create_monster(flecs::world &ecs, Color col, const char *texture_src)
{
  Position pos = find_free_dungeon_tile(ecs);

  flecs::entity textureSrc = ecs.entity(texture_src);
  return ecs.entity()
    .set(Position{pos.x, pos.y})
    .set(MovePos{pos.x, pos.y})
    .set(Hitpoints{100.f})
    .set(Action{EA_NOP})
    .set(Color{col})
    .add<TextureSource>(textureSrc)
    .set(StateMachine{})
    .set(Team{1})
    .set(NumActions{1, 0})
    .set(MeleeDamage{20.f})
    .set(Blackboard{});
}

static flecs::entity create_character(flecs::world &ecs, Color col, const char *texture_src, size_t id, const Position &pos)
{
  //Position pos = find_free_dungeon_tile(ecs);

  flecs::entity textureSrc = ecs.entity(texture_src);
  return ecs.entity()
    .set(Position{pos.x, pos.y})
    .set(MovePos{pos.x, pos.y})
    .set(StartPos{pos.x, pos.y})
    .set(Action{EA_NOP})
    .set(Color{col})
    .add<TextureSource>(textureSrc)
    .set(StateMachine{})
    .set(NumActions{1, 0})
    .set(Blackboard{})
    .set(Hunger{100.0f/2.0f})
    .set(Comfort{100.0f/2.0f})
    .set(Social{100.0f/2.0f})
    .set(Hygiene{100.0f/2.0f})
    .set(Fun{100.0f/2.0f})
    .set(Energy{100.0f/2.0f})
    .set(Bladder{100.0f/2.0f})
    .set(CharacterID{id})
    .set(DmapWeights{{{(std::string("approach_to_item_map_") + std::to_string(id)).c_str(), {1.f, 1.f}}}});;
}

static void create_player(flecs::world &ecs, const char *texture_src, const int &x, const int &y)
{
  //Position pos = find_free_dungeon_tile(ecs);

  flecs::entity textureSrc = ecs.entity(texture_src);
  ecs.entity("player")
    .set(Position{x, y})
    .set(MovePos{x, y})
    //.set(Hitpoints{100.f})
    //.set(Color{0xee, 0xee, 0xee, 0xff})
    .set(Action{EA_NOP})
    .add<IsPlayer>()
    .set(Team{0})
    .set(PlayerInput{})
    .set(NumActions{1, 0})
    .set(Color{255, 255, 255, 255})
    .add<TextureSource>(textureSrc);
    //.set(MeleeDamage{20.f});
}

static void create_heal(flecs::world &ecs, int x, int y, float amount)
{
  ecs.entity()
    .set(Position{x, y})
    .set(HealAmount{amount})
    .set(Color{0xff, 0x44, 0x44, 0xff});
}

static void create_powerup(flecs::world &ecs, int x, int y, float amount)
{
  ecs.entity()
    .set(Position{x, y})
    .set(PowerupAmount{amount})
    .set(Color{0xff, 0xff, 0x00, 0xff});
}

static void register_roguelike_systems(flecs::world &ecs)
{
  static auto dungeonDataQuery = ecs.query<const DungeonData>();
  ecs.system<PlayerInput, Action, const IsPlayer>()
    .each([&](PlayerInput &inp, Action &a, const IsPlayer)
    {
      bool left = IsKeyDown(KEY_LEFT);
      bool right = IsKeyDown(KEY_RIGHT);
      bool up = IsKeyDown(KEY_UP);
      bool down = IsKeyDown(KEY_DOWN);
      if (left && !inp.left)
        a.action = EA_MOVE_LEFT;
      if (right && !inp.right)
        a.action = EA_MOVE_RIGHT;
      if (up && !inp.up)
        a.action = EA_MOVE_UP;
      if (down && !inp.down)
        a.action = EA_MOVE_DOWN;
      inp.left = left;
      inp.right = right;
      inp.up = up;
      inp.down = down;

      bool pass = IsKeyDown(KEY_SPACE);
      if (pass && !inp.passed)
        a.action = EA_PASS;
      inp.passed = pass;
    });
  ecs.system<const Position, const Color>()
    .with<TextureSource>(flecs::Wildcard)
    .with<BackgroundTile>()
    .each([&](flecs::entity e, const Position &pos, const Color color)
    {
      const auto textureSrc = e.target<TextureSource>();
      DrawTextureQuad(*textureSrc.get<Texture2D>(),
          Vector2{1, 1}, Vector2{0, 0},
          Rectangle{float(pos.x) * tile_size, float(pos.y) * tile_size, tile_size, tile_size}, color);
    });
  ecs.system<const Position, const Color>()
    .without<TextureSource>(flecs::Wildcard)
    .each([&](const Position &pos, const Color color)
    {
      const Rectangle rect = {float(pos.x) * tile_size, float(pos.y) * tile_size, tile_size, tile_size};
      DrawRectangleRec(rect, color);
    });
  ecs.system<const Position, const Color>()
    .with<TextureSource>(flecs::Wildcard)
    .without<BackgroundTile>()
    .each([&](flecs::entity e, const Position &pos, const Color color)
    {
      const auto textureSrc = e.target<TextureSource>();
      DrawTextureQuad(*textureSrc.get<Texture2D>(),
          Vector2{1, 1}, Vector2{0, 0},
          Rectangle{float(pos.x) * tile_size, float(pos.y) * tile_size, tile_size, tile_size}, color);
    });
  ecs.system<const Position, const Hitpoints>()
    .each([&](const Position &pos, const Hitpoints &hp)
    {
      constexpr float hpPadding = 0.05f;
      const float hpWidth = 1.f - 2.f * hpPadding;
      const Rectangle underRect = {float(pos.x + hpPadding) * tile_size, float(pos.y-0.25f) * tile_size,
                                   hpWidth * tile_size, 0.1f * tile_size};
      DrawRectangleRec(underRect, BLACK);
      const Rectangle hpRect = {float(pos.x + hpPadding) * tile_size, float(pos.y-0.25f) * tile_size,
                                hp.hitpoints / 100.f * hpWidth * tile_size, 0.1f * tile_size};
      DrawRectangleRec(hpRect, RED);
    });

  ecs.system<Texture2D>()
    .each([&](Texture2D &tex)
    {
      SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    });
  ecs.system<const DmapWeights>()
    .with<VisualiseMap>()
    .each([&](const DmapWeights &wt)
    {
      dungeonDataQuery.each([&](const DungeonData &dd)
      {
        for (size_t y = 0; y < dd.height; ++y)
          for (size_t x = 0; x < dd.width; ++x)
          {
            float sum = 0.f;
            for (const auto &pair : wt.weights)
            {
              ecs.entity(pair.first.c_str()).get([&](const DijkstraMapData &dmap)
              {
                float v = dmap.map[y * dd.width + x];
                if (v < 1e5f)
                  sum += powf(v * pair.second.mult, pair.second.pow);
                else
                  sum += v;
              });
            }
            if (sum < 1e5f)
              DrawText(TextFormat("%.1f", sum),
                  (float(x) + 0.2f) * tile_size, (float(y) + 0.5f) * tile_size, 150, WHITE);
          }
      });
    });
  ecs.system<const DijkstraMapData>()
    .with<VisualiseMap>()
    .each([](const DijkstraMapData &dmap)
    {
      dungeonDataQuery.each([&](const DungeonData &dd)
      {
        for (size_t y = 0; y < dd.height; ++y)
          for (size_t x = 0; x < dd.width; ++x)
          {
            const float val = dmap.map[y * dd.width + x];
            if (val < 1e5f)
              DrawText(TextFormat("%.1f", val),
                  (float(x) + 0.2f) * tile_size, (float(y) + 0.5f) * tile_size, 150, WHITE);
          }
      });
    });
}

void spawn_items_in_house(flecs::world &ecs, std::default_random_engine &rnd, int roomX, int roomY)
{
  std::vector<std::string> listOfNames{
    "armchair", "bath", "bed", "coffeemaker", "computer", "freezer", "library", "mirror",
    "oven", "phone", "sink", "stove", "WC", "shower", "energy-drink", "chess", "chair"
  };

  static auto dungeonDataQuery = ecs.query<DungeonData>();
  dungeonDataQuery.each([&](DungeonData &dungeonData)
  {
    //std::ranges::shuffle(listOfNames, rnd);
    std::shuffle(listOfNames.begin(), listOfNames.end(), rnd);

    int type = GetRandomValue(0, 1);
    int nextTaken = 0;
    for (int x = roomX + 1; x < roomX + 6; ++x)//x += (type + 1))
    {
      for (int y = roomY + 1; y < roomY + 6; ++y)//y += (2 - type))
      {
        if (!((x - roomX == 2 && y - roomY == 2) || (x - roomX == 4 && y - roomY == 4) ||
            (((x - roomX - 1) % (type + 1) == 0) && ((y - roomY - 1) % (2 - type) == 0))))
            continue;
            
        // make item
        dungeonData.tiles[y * dungeonData.width + x] = dungeon::item;

        flecs::entity textureSrc = ecs.entity((listOfNames[nextTaken]+std::string("_tex")).c_str());
        flecs::entity e = ecs.entity()
          .set(Position{x, y})
          .set(Color{0xee, 0x00, 0xee, 0xff})
          .add<TextureSource>(textureSrc)
          .set(NamedItem{listOfNames[nextTaken].c_str()});
      
        // set speed of characteristic recovery
        if (listOfNames[nextTaken] == std::string("oven") || listOfNames[nextTaken] == std::string("stove"))
        {
          e.set(Hunger{7.0f});
        }

        if (listOfNames[nextTaken] == std::string("freezer"))
        {
          e.set(Hunger{5.0f});
        }
        if (listOfNames[nextTaken] == std::string("sink"))
        {
          e.set(Hunger{3.0f});
        }
        if (listOfNames[nextTaken] == std::string("bed") || listOfNames[nextTaken] == std::string("bath"))
        {
          e.set(Comfort{9.0f});
        }
        if (listOfNames[nextTaken] == std::string("chair"))
        {
          e.set(Comfort{5.0f});
        }
        if (listOfNames[nextTaken] == std::string("armchair"))
        {
          e.set(Comfort{7.0f});
          e.set(Energy{6.0f});
        }
        if (listOfNames[nextTaken] == std::string("phone") || listOfNames[nextTaken] == std::string("computer"))
        {
          e.set(Social{7.0f});
        }
        if (listOfNames[nextTaken] == std::string("sink") || listOfNames[nextTaken] == std::string("mirror"))
        {
          e.set(Hygiene{5.0f});
        }
        if (listOfNames[nextTaken] == std::string("mirror"))
        {
          e.set(Social{5.0f});
        }
        if (listOfNames[nextTaken] == std::string("bath") || listOfNames[nextTaken] == std::string("shower"))
        {
          e.set(Hygiene{8.0f});
        }
        if (listOfNames[nextTaken] == std::string("library") || listOfNames[nextTaken] == std::string("phone"))
        {
          e.set(Fun{5.0f});
        }
        if (listOfNames[nextTaken] == std::string("computer") || listOfNames[nextTaken] == std::string("chess"))
        {
          e.set(Fun{7.0f});
        }
        if (listOfNames[nextTaken] == std::string("coffeemaker") || listOfNames[nextTaken] == std::string("energy-drink"))
        {
          e.set(Energy{4.0f});
        }
        if (listOfNames[nextTaken] == std::string("bed"))
        {
          e.set(Energy{8.0f});
        }
        if (listOfNames[nextTaken] == std::string("WC"))
        {
          e.set(Bladder{4.0f});
        }
        
        nextTaken += 1;
        if (nextTaken >= listOfNames.size())
        {
          return;
        }
      }
    }
  });
}

void init_roguelike(flecs::world &ecs, int *roomsCoord)
{
  register_roguelike_systems(ecs);

  ecs.entity("swordsman_tex")
    .set(Texture2D{LoadTexture("assets/swordsman.png")});
  ecs.entity("star_tex")
    .set(Texture2D{LoadTexture("assets/star_contour.png")});
  
  for (std::string& elem : std::vector<std::string>{
    "armchair", "bath", "bed", "coffeemaker", "computer", "freezer", "chair",
    "library", "mirror", "oven", "phone", "sink", "stove", "WC", "shower", "energy-drink", "chess"
  })
  {
    ecs.entity((elem + std::string("_tex")).c_str())
      .set(Texture2D{LoadTexture((std::string("assets/")+elem+std::string(".png")).c_str())});
  }

  ecs.observer<Texture2D>()
    .event(flecs::OnRemove)
    .each([](Texture2D texture)
      {
        UnloadTexture(texture);
      });

  //create_hive_monster(create_monster(ecs, Color{0xee, 0x00, 0xee, 0xff}, "minotaur_tex"));
  //create_hive_monster(create_monster(ecs, Color{0xee, 0x00, 0xee, 0xff}, "minotaur_tex"));
  //create_hive_monster(create_monster(ecs, Color{0x11, 0x11, 0x11, 0xff}, "minotaur_tex"));
  //create_hive(create_player_fleer(create_monster(ecs, Color{0, 255, 0, 255}, "minotaur_tex")));

  for (int i = 0; i < 3; ++i)
  {
    printf("Room coords: %d-%d\n", roomsCoord[2*i], roomsCoord[2*i+1]);

    unsigned seed = unsigned(std::chrono::system_clock::now().time_since_epoch().count() % std::numeric_limits<int>::max());
    std::default_random_engine seedGenerator(seed);

    spawn_items_in_house(ecs, seedGenerator, roomsCoord[2*i], roomsCoord[2*i+1]);
  }
  create_character_beh(create_character(ecs, Color{0xee, 0x00, 0xee, 0xff}, "swordsman_tex", 0, Position{roomsCoord[0], roomsCoord[1]}));
  create_character_beh(create_character(ecs, Color{0x11, 0x11, 0x11, 0xff}, "swordsman_tex", 1, Position{roomsCoord[2], roomsCoord[3]}));
  create_character_beh(create_character(ecs, Color{0x00, 0xff, 0x00, 0xff}, "swordsman_tex", 2, Position{roomsCoord[4], roomsCoord[5]}));

  create_player(ecs, "star_tex", roomsCoord[0], roomsCoord[1]);

  ecs.entity("world")
    .set(TurnCounter{})
    .set(ActionLog{})
    .set(MousePos{0, 0});

  // initialize all maps

  for (size_t id = 0; id < 3; ++id)
  {
    std::vector<float> approachToCharacterMap; // uses to calculate distance
    //dmaps::gen_player_approach_map(ecs, approachToCharacterMap);
    dmaps::gen_character_approach_map(ecs, approachToCharacterMap, id);
    std::string mapName0 = "approach_map_" + std::to_string(id);
    ecs.entity(mapName0.c_str())
      .set(DijkstraMapData{approachToCharacterMap});
  
    std::vector<float> approachToTargetMap;
    std::string mapName1 = "approach_to_item_map_" + std::to_string(id);
    {
      static auto targetPosQuery = ecs.query<const CharacterID, Blackboard>();
      Position targetPos;
      targetPosQuery.each([&](flecs::entity e, const CharacterID &characterId, Blackboard &bb)
      {
        if (characterId.id != id)
        {
          return;
        }
        targetPos = bb.get<Position>("target_position");
      });
      dmaps::gen_specific_approach_map(ecs, approachToTargetMap, targetPos.x, targetPos.y);
    }
    ecs.entity(mapName1.c_str())
      .set(DijkstraMapData{approachToTargetMap});
 
    //std::vector<float> hiveMap;
    //dmaps::gen_hive_pack_map(ecs, hiveMap);
    //ecs.entity("hive_map")
    //  .set(DijkstraMapData{hiveMap});

    //ecs.entity("flee_map").add<VisualiseMap>();
    std::string total_name = "target_approach_map_" + std::to_string(id);
    ecs.entity(total_name.c_str())
      //.set(DmapWeights{{{"flee_map", {1.f, 1.f}}}})
      .set(DmapWeights{{{mapName1.c_str(), {1.f, 1.f}}}});
    //if (id == 0)
    //  ent.add<VisualiseMap>();
  }
}

void init_dungeon(flecs::world &ecs, char *tiles, size_t w, size_t h)
{
  flecs::entity wallTex = ecs.entity("wall_tex")
    .set(Texture2D{LoadTexture("assets/wall.png")});
  flecs::entity floorTex = ecs.entity("floor_tex")
    .set(Texture2D{LoadTexture("assets/floor.png")});

  std::vector<char> dungeonData;
  dungeonData.resize(w * h);
  for (size_t y = 0; y < h; ++y)
    for (size_t x = 0; x < w; ++x)
      dungeonData[y * w + x] = tiles[y * w + x];
  ecs.entity("dungeon")
    .set(DungeonData{dungeonData, w, h});

  for (size_t y = 0; y < h; ++y)
    for (size_t x = 0; x < w; ++x)
    {
      char tile = tiles[y * w + x];
      flecs::entity tileEntity = ecs.entity()
        .add<BackgroundTile>()
        .set(Position{int(x), int(y)})
        .set(Color{255, 255, 255, 255});
      if (tile == dungeon::wall)
        tileEntity.add<TextureSource>(wallTex);
      else // if (tile == dungeon::floor)
        tileEntity.add<TextureSource>(floorTex);
    }
}


static bool is_player_acted(flecs::world &ecs)
{
  static auto processPlayer = ecs.query<const IsPlayer, const Action>();
  bool playerActed = false;
  processPlayer.each([&](const IsPlayer, const Action &a)
  {
    playerActed = a.action != EA_NOP;
  });
  return playerActed;
}

static bool upd_player_actions_count(flecs::world &ecs)
{
  static auto updPlayerActions = ecs.query<const IsPlayer, NumActions>();
  bool actionsReached = false;
  updPlayerActions.each([&](const IsPlayer, NumActions &na)
  {
    na.curActions = (na.curActions + 1) % na.numActions;
    actionsReached |= na.curActions == 0;
  });
  return actionsReached;
}

static Position move_pos(Position pos, int action)
{
  if (action == EA_MOVE_LEFT)
    pos.x--;
  else if (action == EA_MOVE_RIGHT)
    pos.x++;
  else if (action == EA_MOVE_UP)
    pos.y--;
  else if (action == EA_MOVE_DOWN)
    pos.y++;
  return pos;
}

static void push_to_log(flecs::world &ecs, const char *msg)
{
  static auto queryLog = ecs.query<ActionLog, const TurnCounter>();
  queryLog.each([&](ActionLog &l, const TurnCounter &c)
  {
    l.log.push_back(std::to_string(c.count) + ": " + msg);
    if (l.log.size() > l.capacity)
      l.log.erase(l.log.begin());
  });
}

template <typename T>
static void clampValue(T& characterstic, float minVal, float maxVal)
{
  characterstic.value = std::min(maxVal, std::max(minVal, characterstic.value));
}

static void process_actions(flecs::world &ecs)
{
  static auto processActions =  ecs.query<Action, Position, MovePos,
                                          Hunger, Comfort, Social,
                                          Hygiene, Fun, Energy, Bladder>();
  static auto checkHungerInteraction = ecs.query<Hunger, const Position, const NamedItem>();
  static auto checkComfortInteraction = ecs.query<Comfort, const Position, const NamedItem>();
  static auto checkSocialInteraction = ecs.query<Social, const Position, const NamedItem>();
  static auto checkHygieneInteraction = ecs.query<Hygiene, const Position, const NamedItem>();
  static auto checkFunInteraction = ecs.query<Fun, const Position, const NamedItem>();
  static auto checkEnergyInteraction = ecs.query<Energy, const Position, const NamedItem>();
  static auto checkBladderInteraction = ecs.query<Bladder, const Position, const NamedItem>();

  static auto processPlayer = ecs.query<Action, Position, MovePos, const Team>();

  // Process all actions
  ecs.defer([&]
  {
    processActions.each([&](flecs::entity entity, Action &a, Position &pos, MovePos &mpos,
                            Hunger &hunger, Comfort &comfort, Social &social, Hygiene &hygiene,
                            Fun &fun, Energy &energy, Bladder &bladder)
    {
      // Hardcode: decSpeed is the amount by which all characteristics are reduced at each turn
      // (except increasing cheracteristic)
      const float decSpeed = 0.25f;
      Position nextPos = move_pos(pos, a.action);
      //printf("Action: %d. NextPos %d-%d\n", a.action, nextPos.x, nextPos.y);
      bool blocked = !dungeon::is_tile_walkable(ecs, nextPos);
      //if (blocked) printf("Wall or item here!\n");

      checkHungerInteraction.each([&](Hunger &hungerInc, const Position &itemPos, const NamedItem)
      {
        if (mpos == itemPos)
        {
          if (a.action == EA_INC_HUNGER)
          {
            hunger.value += (hungerInc.value + decSpeed);
            push_to_log(ecs, "Hunger increased.");
            blocked = true;
          }
        }
      });
      hunger.value -= decSpeed;
      clampValue(hunger, 0.0f, 100.0f);

      checkSocialInteraction.each([&](Social &socialInc, const Position &itemPos, const NamedItem)
      {
        if (mpos == itemPos)
        {
          if (a.action == EA_INC_SOCIAL)
          {
            social.value += (socialInc.value + decSpeed);
            push_to_log(ecs, "Social increased.");
            blocked = true;
          }
        }
      });
      social.value -= decSpeed;
      clampValue(social, 0.0f, 100.0f);

      checkComfortInteraction.each([&](Comfort &comfortInc, const Position &itemPos, const NamedItem)
      {
          if (mpos == itemPos && a.action == EA_INC_COMFORT)
          {
            comfort.value += (comfortInc.value + decSpeed);
            push_to_log(ecs, "Comfort increased.");
            blocked = true;
          }
      });
      comfort.value -= decSpeed;
      clampValue(comfort, 0.0f, 100.0f);

      checkHygieneInteraction.each([&](Hygiene &hygieneInc, const Position &itemPos, const NamedItem)
      {
        if (mpos == itemPos && a.action == EA_INC_HYGIENE)
        {
          hygiene.value += (hygieneInc.value + decSpeed);
          push_to_log(ecs, "Hygiene increased.");
          blocked = true;
        }
      });
      hygiene.value -= decSpeed;
      clampValue(hygiene, 0.0f, 100.0f);

      checkFunInteraction.each([&](Fun &funInc, const Position &itemPos, const NamedItem)
      {
        if (mpos == itemPos && a.action == EA_INC_FUN)
        {
          fun.value += (funInc.value + decSpeed);
          push_to_log(ecs, "Fun increased.");
          blocked = true;
        }
      });
      fun.value -= decSpeed;
      clampValue(fun, 0.0f, 100.0f);

      checkEnergyInteraction.each([&](Energy &energyInc, const Position &itemPos, const NamedItem)
      {
        if (mpos == itemPos && a.action == EA_INC_ENERGY)
        {
          energy.value += (energyInc.value + decSpeed);
          push_to_log(ecs, "Energy increased.");
          blocked = true;
        }
      });
      energy.value -= decSpeed;
      clampValue(energy, 0.0f, 100.0f);

      checkBladderInteraction.each([&](Bladder &bladderInc, const Position &itemPos, const NamedItem)
      {
        if (mpos == itemPos && a.action == EA_INC_BLADDER)
        {
          bladder.value += (bladderInc.value + decSpeed);
          push_to_log(ecs, "Bladder increased.");
          blocked = true;
        }
      });
      bladder.value -= decSpeed;
      clampValue(bladder, 0.0f, 100.0f);

      if (!blocked)
      {
        // move characters
        mpos = nextPos;
        pos = mpos;
      }
      else
      {
        //printf("I interact with smth. Mpos: %d-%d. NextPos: %d-%d. Action: %d\n", mpos.x, mpos.y, nextPos.x, nextPos.y, a.action);
        mpos = pos;
      }
      //a.action = EA_NOP;
    });

    // now move
    processPlayer.each([&](Action &a, Position &pos, MovePos &mpos, const Team&)
    {
      Position nextPos = move_pos(pos, a.action);
      bool blocked = !dungeon::is_tile_walkable(ecs, nextPos);
      if (!blocked)
      {
        mpos = nextPos;
      }

      pos = mpos;
      a.action = EA_NOP;
    });
  });
}

template<typename T>
static void push_info_to_bb(Blackboard &bb, const char *name, const T &val)
{
  size_t idx = bb.regName<T>(name);
  bb.set(idx, val);
}


template<typename T>
static void calculateNearest(flecs::world &ecs, const CharacterID &charId, Blackboard &bb,
                              const Position &pos, const char *nearestName)
{
  static auto itemsQuery = ecs.query<const T, const Position, const NamedItem>();
  static auto dungeonDataQuery = ecs.query<const DungeonData>();
  float closestItemDist = 100.0f;

  std::string mapName = "approach_map_"+std::to_string(charId.id);
  
  ecs.entity(mapName.c_str()).get([&](const DijkstraMapData &dmap)
  {
    dungeonDataQuery.each([&](const DungeonData &dd)
    {
      itemsQuery.each([&](const T &valForInc, const Position &ipos, const NamedItem)
      {
        float curVal = dmap.map[ipos.y * dd.width + ipos.x];
        if (curVal < closestItemDist)
        {
          closestItemDist = curVal;
        }
      });
    });
  });
  
  push_info_to_bb(bb, nearestName, closestItemDist);
  return;
}

static void calculateAllItemsDist(flecs::world &ecs, const CharacterID &charId, Blackboard &bb,
  const Position &pos)
{
  static auto itemsQuery = ecs.query<const Position, const NamedItem>();
  static auto dungeonDataQuery = ecs.query<const DungeonData>();

  std::string mapName = "approach_map_"+std::to_string(charId.id);
  
  ecs.entity(mapName.c_str()).get([&](const DijkstraMapData &dmap)
  {
    dungeonDataQuery.each([&](const DungeonData &dd)
    {
      itemsQuery.each([&](const Position &ipos, const NamedItem &name)
      {
        float curVal = dmap.map[ipos.y * dd.width + ipos.x];
        float lastDist = bb.get<float>(name.name.c_str());
        if (curVal < lastDist || lastDist <= 0.0f)
        {
          push_info_to_bb(bb, name.name.c_str(), curVal);
//          printf("{%s} - %f\n", name.name.c_str(), curVal);
        }
      });
    });
  });
  return;
}

// sensors
static void gather_world_info(flecs::world &ecs)
{
  static auto gatherWorldInfo = ecs.query<Blackboard,
                                          const Position, const Hunger,
                                          const Comfort, const Social,
                                          const Hygiene, const Fun,
                                          const Energy, const Bladder,
                                          const CharacterID,
                                          const WorldInfoGatherer>();
  static auto alliesQuery = ecs.query<const Position, const Team>();
  gatherWorldInfo.each([&](Blackboard &bb, const Position &pos, 
                          const Hunger &hunger, const Comfort &comfort, const Social &social,
                          const Hygiene &hygiene, const Fun &fun, const Energy &energy,
                          const Bladder &bladder, const CharacterID &charId, WorldInfoGatherer)
  {
    // first gather all needed names (without cache)

    push_info_to_bb(bb, "hunger", hunger.value);
    push_info_to_bb(bb, "comfort", comfort.value);
    push_info_to_bb(bb, "social", social.value);
    push_info_to_bb(bb, "hygiene", hygiene.value);
    push_info_to_bb(bb, "fun", fun.value);
    push_info_to_bb(bb, "energy", energy.value);
    push_info_to_bb(bb, "bladder", bladder.value);


    // find distance to nearest item in each category
    // and distance between character and nearest most useful

    calculateNearest<Hunger>(ecs, charId, bb, pos, "nearest_hunger_item_dist");
    calculateNearest<Comfort>(ecs, charId, bb, pos, "nearest_comfort_item_dist");
    calculateNearest<Social>(ecs, charId, bb, pos, "nearest_social_item_dist");
    calculateNearest<Hygiene>(ecs, charId, bb, pos, "nearest_hygiene_item_dist");
    calculateNearest<Fun>(ecs, charId, bb, pos, "nearest_fun_item_dist");
    calculateNearest<Energy>(ecs, charId, bb, pos, "nearest_energy_item_dist");
    calculateNearest<Bladder>(ecs, charId, bb, pos, "nearest_bladder_item_dist");

    calculateAllItemsDist(ecs, charId, bb, pos);
  });
}

void process_turn(flecs::world &ecs)
{
  static auto stateMachineAct = ecs.query<StateMachine>();
  static auto behTreeUpdate = ecs.query<BehaviourTree, Blackboard>();
  static auto turnIncrementer = ecs.query<TurnCounter>();
  if (is_player_acted(ecs))
  {
    if (upd_player_actions_count(ecs))
    {
      // Plan action for NPCs
      gather_world_info(ecs);
      ecs.defer([&]
      {
        stateMachineAct.each([&](flecs::entity e, StateMachine &sm)
        {
          sm.act(0.f, ecs, e);
        });
        behTreeUpdate.each([&](flecs::entity e, BehaviourTree &bt, Blackboard &bb)
        {
          bt.update(ecs, e, bb);
        });
        process_dmap_followers(ecs); // real move of character here
      });
      turnIncrementer.each([](TurnCounter &tc) { tc.count++; });
    }
    process_actions(ecs);

    for (size_t id = 0; id < 3; ++id)
    {
      std::vector<float> approachToCharacterMap; // uses to calculate distance
      //dmaps::gen_player_approach_map(ecs, approachToCharacterMap);
      dmaps::gen_character_approach_map(ecs, approachToCharacterMap, id);
      std::string mapName0 = "approach_map_" + std::to_string(id);
      ecs.entity(mapName0.c_str())
        .set(DijkstraMapData{approachToCharacterMap});
  
      std::vector<float> approachToTargetMap;
      std::string mapName1 = "approach_to_item_map_" + std::to_string(id);
      {
        static auto targetPosQuery = ecs.query<const CharacterID, Blackboard>();
        Position targetPos;
        targetPosQuery.each([&](flecs::entity e, const CharacterID &characterId, Blackboard &bb)
        {
          if (characterId.id != id)
          {
            return;
          }
          targetPos = bb.get<Position>("target_position");
        });
        dmaps::gen_specific_approach_map(ecs, approachToTargetMap, targetPos.x, targetPos.y);
      }
      ecs.entity(mapName1.c_str())
        .set(DijkstraMapData{approachToTargetMap});
  
      //std::vector<float> hiveMap;
      //dmaps::gen_hive_pack_map(ecs, hiveMap);
      //ecs.entity("hive_map")
      //  .set(DijkstraMapData{hiveMap});
  
      //ecs.entity("flee_map").add<VisualiseMap>();
      std::string total_name = "target_approach_map_" + std::to_string(id);
      ecs.entity(total_name.c_str())
        //.set(DmapWeights{{{"flee_map", {1.f, 1.f}}}})
        .set(DmapWeights{{{mapName1.c_str(), {1.f, 1.f}}}});
      //if (id == 0)
      //  ent.add<VisualiseMap>();
    }
  }
}

void print_stats(flecs::world &ecs)
{
  /*static auto playerStatsQuery = ecs.query<const IsPlayer, const Hitpoints, const MeleeDamage>();
  playerStatsQuery.each([&](const IsPlayer &, const Hitpoints &hp, const MeleeDamage &dmg)
  {
    DrawText(TextFormat("hp: %d", int(hp.hitpoints)), 20, 20, 20, WHITE);
    DrawText(TextFormat("power: %d", int(dmg.damage)), 20, 40, 20, WHITE);
  });*/
  
  static auto mouseInfoQuery = ecs.query<const MousePos>();
  static auto charactersQuery = ecs.query<const Position, const Hunger, const Comfort, const Social, const Hygiene, const Fun, const Energy, const Bladder>();
  static auto itemsNamesQuery = ecs.query<const Position, const NamedItem>();

  mouseInfoQuery.each([&](const MousePos &mpos)
  {
    DrawText(TextFormat("Mouse position x: %d", mpos.x), 20, 20, 20, WHITE);
    DrawText(TextFormat("Mouse position y: %d", mpos.y), 20, 40, 20, WHITE);

    charactersQuery.each([&](const Position &pos, const Hunger &hunger, const Comfort &comfort,
                            const Social &social, const Hygiene &hygiene, const Fun &fun,
                            const Energy &energy, const Bladder &bladder)
    {
      if (pos.x == mpos.x && pos.y == mpos.y)
      {
        int yPos = 60;
        DrawText(TextFormat("Hunger:\t%d", (int) hunger.value), 20, yPos, 20, WHITE);
        yPos += 20;
        DrawText(TextFormat("Comfort:\t%d", (int) comfort.value), 20, yPos, 20, WHITE);
        yPos += 20;
        DrawText(TextFormat("Social:\t%d", (int) social.value), 20, yPos, 20, WHITE);
        yPos += 20;
        DrawText(TextFormat("Hygiene:\t%d", (int) hygiene.value), 20, yPos, 20, WHITE);
        yPos += 20;
        DrawText(TextFormat("Fun:\t%d", (int) fun.value), 20, yPos, 20, WHITE);
        yPos += 20;
        DrawText(TextFormat("Energy:\t%d", (int) energy.value), 20, yPos, 20, WHITE);
        yPos += 20;
        DrawText(TextFormat("Bladder:\t%d", (int) bladder.value), 20, yPos, 20, WHITE);
      }
    });

    itemsNamesQuery.each([&](const Position &pos, const NamedItem &namedItem)
    {
      if (pos.x == mpos.x && pos.y == mpos.y)
      {
        DrawText(namedItem.name.c_str(), 20, 60, 20, WHITE);
      }
    });
  });

  /*static auto playerQuery = ecs.query<const IsPlayer, const Position>();
  playerQuery.each([&](const IsPlayer&, const Position& pos)
  {
    DrawText(TextFormat("Player position x: %d", int(pos.x)), 20, 100, 20, WHITE);
    DrawText(TextFormat("Player position y: %d", int(pos.y)), 20, 120, 20, WHITE);
  });*/

  static auto actionLogQuery = ecs.query<const ActionLog>();
  actionLogQuery.each([&](const ActionLog &l)
  {
    int yPos = GetRenderHeight() - 20;
    for (const std::string &msg : l.log)
    {
      DrawText(msg.c_str(), 20, yPos, 20, WHITE);
      yPos -= 20;
    }
  });
}

