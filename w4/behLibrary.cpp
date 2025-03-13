#include "aiLibrary.h"
#include "ecsTypes.h"
#include "aiUtils.h"
#include "math.h"
#include "raylib.h"
#include "blackboard.h"
#include <algorithm>

struct CompoundNode : public BehNode
{
  std::vector<BehNode*> nodes;

  virtual ~CompoundNode()
  {
    for (BehNode *node : nodes)
      delete node;
    nodes.clear();
  }

  CompoundNode &pushNode(BehNode *node)
  {
    nodes.push_back(node);
    return *this;
  }
};

struct Sequence : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_SUCCESS)
        return res;
    }
    return BEH_SUCCESS;
  }
};

struct Selector : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_FAIL)
        return res;
    }
    return BEH_FAIL;
  }
};

struct UtilitySelector : public BehNode
{
  std::vector<std::pair<BehNode*, utility_function>> utilityNodes;

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    std::vector<std::pair<float, size_t>> utilityScores;
    for (size_t i = 0; i < utilityNodes.size(); ++i)
    {
      const float utilityScore = utilityNodes[i].second(bb);
      utilityScores.push_back(std::make_pair(utilityScore, i));
    }
    std::sort(utilityScores.begin(), utilityScores.end(), [](auto &lhs, auto &rhs)
    {
      return lhs.first > rhs.first;
    });
    for (const std::pair<float, size_t> &node : utilityScores)
    {
      size_t nodeIdx = node.second;
      BehResult res = utilityNodes[nodeIdx].first->update(ecs, entity, bb);
      if (res != BEH_FAIL)
        return res;
    }
    return BEH_FAIL;
  }
};

struct UtilitySelectorWithSmallRandom : public BehNode
{
  std::vector<std::pair<BehNode*, utility_function>> utilityNodes;
  float theshold = 5.0f;

  std::vector<float> multipliersForInertiaInUtilityFunc;
  std::vector<int> cooldownInUtilityFunc;

  const int maxCooldown = 10;

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    std::vector<std::pair<float, size_t>> utilityScores;
    
    // multiplyers for inertia and cooldown
    
    if (multipliersForInertiaInUtilityFunc.size() < utilityNodes.size()) {
      multipliersForInertiaInUtilityFunc.clear();
      multipliersForInertiaInUtilityFunc.reserve(utilityNodes.size());
      for (int i = 0; i < utilityNodes.size(); ++i) {
        multipliersForInertiaInUtilityFunc.push_back(1.0f);
      }
    } else {
      for (int i = 0; i < multipliersForInertiaInUtilityFunc.size(); ++i) {
        multipliersForInertiaInUtilityFunc[i] = std::max(1.0f, multipliersForInertiaInUtilityFunc[i] - 0.1f);
      }
    }

    if (cooldownInUtilityFunc.size() < utilityNodes.size()) {
      cooldownInUtilityFunc.clear();
      cooldownInUtilityFunc.reserve(utilityNodes.size());
      for (int i = 0; i < utilityNodes.size(); ++i) {
        cooldownInUtilityFunc.push_back(0);
      }
    } else {
      for (int i = 0; i < cooldownInUtilityFunc.size(); ++i) {
        cooldownInUtilityFunc[i] = std::max(0, cooldownInUtilityFunc[i] - 1);
      }
    }

    for (size_t i = 0; i < utilityNodes.size(); ++i)
    {
      const float utilityScore = utilityNodes[i].second(bb);
      utilityScores.push_back(std::make_pair(
                    multipliersForInertiaInUtilityFunc[i] * ((cooldownInUtilityFunc[i] == maxCooldown - 1) * 1.0f
                    + (cooldownInUtilityFunc[i] != maxCooldown - 1) * (1.0f - cooldownInUtilityFunc[i] / maxCooldown * 0.9f)) *
                    utilityScore,
                  i));
    }
    std::sort(utilityScores.begin(), utilityScores.end(), [](auto &lhs, auto &rhs)
    {
      return lhs.first > rhs.first;
    });

    float minUtilityForRandom = utilityScores[0].first - theshold;
    // Find the number of elements from which the random one will be selected
    size_t countOfElementsWithBigUtility = 1;
    while ((countOfElementsWithBigUtility < utilityScores.size()) && (utilityScores[countOfElementsWithBigUtility].first >= minUtilityForRandom))
      ++countOfElementsWithBigUtility;
    
    size_t firstChosenIdx = utilityScores[GetRandomValue(0, countOfElementsWithBigUtility - 1)].second;
    BehResult res = utilityNodes[firstChosenIdx].first->update(ecs, entity, bb);
    if (res != BEH_FAIL)
    {
      cooldownInUtilityFunc[firstChosenIdx] = maxCooldown;
      multipliersForInertiaInUtilityFunc[firstChosenIdx] = 1.7f;
      return res;
    }

    for (const std::pair<float, size_t> &node : utilityScores)
    {
      size_t nodeIdx = node.second;
      if (nodeIdx == firstChosenIdx)
        continue;
      
      res = utilityNodes[nodeIdx].first->update(ecs, entity, bb);
      if (res != BEH_FAIL)
      {
        cooldownInUtilityFunc[nodeIdx] = maxCooldown;
        multipliersForInertiaInUtilityFunc[nodeIdx] = 1.7f;
        return res;
      }
    }
    return BEH_FAIL;
  }
};

struct UtilitySelectorWithPriority : public BehNode
{
  // std::function<float(Blackboard&)> - it's function for priority
  std::vector<std::pair<BehNode*, std::pair<utility_function, std::function<int(Blackboard&)>>>> utilityNodes;
  std::vector<float> multipliersForInertiaInUtilityFunc;
  std::vector<int> cooldownInUtilityFunc;

  const int maxCooldown = 20;

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    // multiplyers for inertia and cooldown
    
    if (multipliersForInertiaInUtilityFunc.size() < utilityNodes.size()) {
      multipliersForInertiaInUtilityFunc.clear();
      multipliersForInertiaInUtilityFunc.reserve(utilityNodes.size());
      for (int i = 0; i < utilityNodes.size(); ++i) {
        multipliersForInertiaInUtilityFunc.push_back(1.0f);
      }
    } else {
      for (int i = 0; i < multipliersForInertiaInUtilityFunc.size(); ++i) {
        multipliersForInertiaInUtilityFunc[i] = std::max(1.0f, multipliersForInertiaInUtilityFunc[i] - 0.1f);
      }
    }

    if (cooldownInUtilityFunc.size() < utilityNodes.size()) {
      cooldownInUtilityFunc.clear();
      cooldownInUtilityFunc.reserve(utilityNodes.size());
      for (int i = 0; i < utilityNodes.size(); ++i) {
        cooldownInUtilityFunc.push_back(0);
      }
    } else {
      for (int i = 0; i < cooldownInUtilityFunc.size(); ++i) {
        cooldownInUtilityFunc[i] = std::max(0, cooldownInUtilityFunc[i] - 1);
      }
    }

    std::vector<std::pair<std::pair<float, int>, size_t>> utilityScoresAndPriorities; // (score-priority)-idx

    for (size_t i = 0; i < utilityNodes.size(); ++i)
    {
      const float utilityScore = utilityNodes[i].second.first(bb);
      const float priorityScore = utilityNodes[i].second.second(bb);
      utilityScoresAndPriorities.push_back(std::make_pair(std::make_pair(
                    multipliersForInertiaInUtilityFunc[i] * ((cooldownInUtilityFunc[i] == maxCooldown - 1) * 1.0f
                            + (cooldownInUtilityFunc[i] != maxCooldown - 1) * (1.0f - 1.0f * cooldownInUtilityFunc[i] / maxCooldown * 0.5f)) *
                            utilityScore, priorityScore), i));
    }

    std::sort(utilityScoresAndPriorities.begin(), utilityScoresAndPriorities.end(), [](auto &lhs, auto &rhs)
    {
      if (lhs.first.second != rhs.first.second)
        return lhs.first.second > rhs.first.second; // Priorities comparation
      return lhs.first.first > rhs.first.first; // Utility scores comparation
    }); // Because the sorting is unstable, I expect the relative order to change occasionally. But I'm not sure)

    for (const std::pair<std::pair<float, int>, size_t> &node : utilityScoresAndPriorities)
    {
      size_t nodeIdx = node.second;
      BehResult res = utilityNodes[nodeIdx].first->update(ecs, entity, bb);
      
      if (res != BEH_FAIL)
      {
        multipliersForInertiaInUtilityFunc[nodeIdx] = 2.0f;
        return res;
      }
    }
    return BEH_FAIL;
  }
};

struct MoveToEntity : public BehNode
{
  size_t entityBb = size_t(-1); // wraps to 0xff...
  MoveToEntity(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.insert([&](Action &a, const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }
      targetEntity.get([&](const Position &target_pos)
      {
        if (pos != target_pos)
        {
          a.action = move_towards(pos, target_pos);
          res = BEH_RUNNING;
        }
        else
          res = BEH_SUCCESS;
      });
    });
    return res;
  }
};

struct MoveToEntityWithMap : public BehNode
{
  size_t entityBb = size_t(-1); // wraps to 0xff...
  size_t targetPosForCharacter = size_t(-1);

  MoveToEntityWithMap(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
    targetPosForCharacter = reg_entity_blackboard_var<Position>(entity, "target_position");
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.insert([&](Action &a, const Position &pos, MovePos &mpos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }

      targetEntity.get([&](const Position &target_pos)
      {
        if ((pos.x + 1 == target_pos.x && pos.y == target_pos.y) ||
            (pos.x - 1 == target_pos.x && pos.y == target_pos.y) ||
            (pos.x == target_pos.x && pos.y + 1 == target_pos.y) ||
            (pos.x == target_pos.x && pos.y - 1 == target_pos.y))
        {
          res = BEH_SUCCESS;
          //a.action = act_at_connect.action; //EA_INC_HUNGER or EA_INC_COMFORT or etc.
          // mpos = target_pos;
        }
        else
        {
          //a.action = move_towards(pos, target_pos);
          bb.set(targetPosForCharacter, target_pos);
          res = BEH_RUNNING;
        }
      });
    });
    return res;
  }
};

// be careful - here visit flag!
struct MoveToPos : public BehNode
{
  size_t entityBb = size_t(-1); // wraps to 0xff...
  size_t visitBb = size_t(-1);
  size_t meetBb = size_t(-1);
  size_t targetPosForCharacter = size_t(-1);
  bool isReturning = false;

  MoveToPos(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<Position>(entity, bb_name);
    isReturning = (bb_name[0]=='h');
    targetPosForCharacter = reg_entity_blackboard_var<Position>(entity, "target_position");
    visitBb = reg_entity_blackboard_var<int>(entity, "visit");
    meetBb = reg_entity_blackboard_var<int>(entity, "meet");
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    if (!isReturning && 0 != bb.get<int>("meet"))
    {
      return BEH_SUCCESS; // we already pass this way
    } else printf("I go\n");

    BehResult res = BEH_RUNNING;
    Position target_pos;
    entity.insert([&](Action &a, const Position &pos, MovePos &mpos, StartPos &spos)
    {
      target_pos = bb.get<Position>(entityBb);
      if (pos == target_pos || (pos.x + 1 == target_pos.x && pos.y == target_pos.y) ||
      (pos.x - 1 == target_pos.x && pos.y == target_pos.y) || (pos.x == target_pos.x && pos.y + 1== target_pos.y)
      || (pos.x == target_pos.x && pos.y - 1== target_pos.y))
      {
        res = BEH_SUCCESS;
        //printf("Reached %d-%d -> %d-%d\n", pos.x, pos.y, target_pos.x, target_pos.y);
        if (spos.x == target_pos.x && spos.y == target_pos.y)
        {
          bb.set<int>(visitBb, 0);
          bb.set<int>(meetBb, 0);
        }
      }
      else
      {
        //a.action = move_towards(pos, target_pos);
        bb.set<Position>(targetPosForCharacter, target_pos);
        //printf("Move to %d-%d\n", target_pos.x, target_pos.y);
        bb.set<int>(visitBb, 1);
        res = BEH_RUNNING;
      }
    });
    return res;
  }
};

struct IncValue : public BehNode
{
  size_t entityBb = size_t(-1); // wraps to 0xff...
  Action curAct = Action{EA_NOP};
  std::string charactName;

  IncValue(flecs::entity entity, const char *bb_name, const Action& action_at_connection, const std::string& charName)
          : curAct{action_at_connection}, charactName(charName)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.insert([&](Action &a, const Position &pos, MovePos &mpos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }

      targetEntity.get([&](const Position &target_pos)
      {
        if ((pos.x + 1 == target_pos.x && pos.y == target_pos.y) ||
            (pos.x - 1 == target_pos.x && pos.y == target_pos.y) ||
            (pos.x == target_pos.x && pos.y + 1 == target_pos.y) ||
            (pos.x == target_pos.x && pos.y - 1 == target_pos.y))
        {
          res = BEH_RUNNING;
          a.action = curAct.action; // EA_INC_HUNGER or EA_INC_COMFORT or etc.
          mpos = target_pos; // to check interaction
        }
        else
        {
          //a.action = move_towards(pos, target_pos);
          //bb.set(targetPosForCharacter, target_pos);
          res = BEH_FAIL;
        }
      });

      if (res == BEH_RUNNING and 100.0f <= bb.get<float>(charactName.c_str()))
      {
        res = BEH_SUCCESS;
      }
    });
    return res;
  }
};

struct IncSocialOnce : public BehNode
{
  float incValue = 10.0f;
  size_t meetBb = size_t(-1);

  IncSocialOnce(flecs::entity entity, float incValue)
        : incValue(incValue)
  {
    meetBb = reg_entity_blackboard_var<int>(entity, "meet");
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    if (bb.get<int>("meet"))
      return BEH_SUCCESS;

    entity.insert([&](Social &social)
    {
      social.value += incValue;
    });
    bb.set<int>(meetBb, 1);
    return BEH_SUCCESS;
  }
};

struct IsLowHp : public BehNode
{
  float threshold = 0.f;
  IsLowHp(float thres) : threshold(thres) {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &) override
  {
    BehResult res = BEH_SUCCESS;
    entity.get([&](const Hitpoints &hp)
    {
      res = hp.hitpoints < threshold ? BEH_SUCCESS : BEH_FAIL;
    });
    return res;
  }
};

struct FindEnemy : public BehNode
{
  size_t entityBb = size_t(-1);
  float distance = 0;
  FindEnemy(flecs::entity entity, float in_dist, const char *bb_name) : distance(in_dist)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    static auto enemiesQuery = ecs.query<const Position, const Team>();
    entity.insert([&](const Position &pos, const Team &t)
    {
      flecs::entity closestEnemy;
      float closestDist = FLT_MAX;
      Position closestPos;
      enemiesQuery.each([&](flecs::entity enemy, const Position &epos, const Team &et)
      {
        if (t.team == et.team)
          return;
        float curDist = dist(epos, pos);
        if (curDist < closestDist)
        {
          closestDist = curDist;
          closestPos = epos;
          closestEnemy = enemy;
        }
      });
      if (ecs.is_valid(closestEnemy) && closestDist <= distance)
      {
        bb.set<flecs::entity>(entityBb, closestEnemy);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }
};

struct FindCharacter : public BehNode
{
  size_t entityBb = size_t(-1);
  
  FindCharacter(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<Position>(entity, bb_name);
  }
  
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    static auto charactersQuery = ecs.query<const Position, const CharacterID>();
    entity.insert([&](const Position &pos, const CharacterID &characterId)
    {
      flecs::entity charact;
      Position pos_of_char;
      charactersQuery.each([&](flecs::entity friendChar, const Position &epos, const CharacterID &otherCharId)
      {
        if (characterId.id == otherCharId.id || !friendChar.is_alive())
          return;
        charact = friendChar;
        pos_of_char = epos;
      });

      if (ecs.is_valid(charact))
      {
        bb.set<Position>(entityBb, pos_of_char);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }
};

struct FindHome : public BehNode
{
  size_t entityBb = size_t(-1);
  
  FindHome(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<Position>(entity, bb_name);
  }
  
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_SUCCESS;
    entity.insert([&](const StartPos &spos)
    {
      bb.set<Position>(entityBb, Position{spos.x, spos.y});
    });
    return res;
  }
};

struct FindNamedItem : public BehNode
{
  size_t entityBb = size_t(-1);
  std::string nameOfItem;

  FindNamedItem(flecs::entity entity, const char *bb_name, const std::string &name) : nameOfItem(name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    static auto itemsQuery = ecs.query<const Position, const NamedItem>();
    static auto dungeonDataQuery = ecs.query<const DungeonData>();

    entity.insert([&](const Position &pos, const CharacterID &characterId)
    {
      std::string mapName = "approach_map_"+std::to_string(characterId.id);
      flecs::entity closestItemWithName;
      float closestDist = FLT_MAX;
      Position closestPos;

      ecs.entity(mapName.c_str()).get([&](const DijkstraMapData &dmap)
      {
        dungeonDataQuery.each([&](const DungeonData &dd)
        {
          itemsQuery.each([&](flecs::entity item, const Position &ipos, const NamedItem &name)
          {
            if (name.name != nameOfItem)
              return;
            
            float curDist = dmap.map[ipos.y * dd.width + ipos.x];
            if (curDist < closestDist)
            {
              closestDist = curDist;
              closestPos = ipos;
              closestItemWithName = item;
            }
          });
        });
      });
      if (ecs.is_valid(closestItemWithName))
      {
        bb.set<flecs::entity>(entityBb, closestItemWithName);
        res = BEH_SUCCESS;
      }
    });

    return res;
  }
};

template<typename T>
struct FindNearestItemWithTypeT : public BehNode
{
  size_t entityBb = size_t(-1);

  FindNearestItemWithTypeT(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    static auto itemsQuery = ecs.query<const Position, const T, const NamedItem>();
    static auto dungeonDataQuery = ecs.query<const DungeonData>();

    entity.insert([&](const Position &pos, const T &, const CharacterID &characterId)
    {
      std::string mapName = "approach_map_"+std::to_string(characterId.id);
      flecs::entity closestItemWithT;
      float closestDist = FLT_MAX;
      Position closestPos;

      ecs.entity(mapName.c_str()).get([&](const DijkstraMapData &dmap)
      {
        dungeonDataQuery.each([&](const DungeonData &dd)
        {
          itemsQuery.each([&](flecs::entity item, const Position &ipos, const T &valForInc, const NamedItem)
          {
            float curDist = dmap.map[ipos.y * dd.width + ipos.x];
            if (curDist < closestDist)
            {
              closestDist = curDist;
              closestPos = ipos;
              closestItemWithT = item;
            }
          });
        });
      });
      if (ecs.is_valid(closestItemWithT))
      {
        bb.set<flecs::entity>(entityBb, closestItemWithT);
//        printf("Nearest ?: %d-%d\n", closestPos.x, closestPos.y);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }
};

struct Flee : public BehNode
{
  size_t entityBb = size_t(-1);
  Flee(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.insert([&](Action &a, const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }
      targetEntity.get([&](const Position &target_pos)
      {
        a.action = inverse_move(move_towards(pos, target_pos));
      });
    });
    return res;
  }
};

struct Patrol : public BehNode
{
  size_t pposBb = size_t(-1);
  float patrolDist = 1.f;
  Patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
    : patrolDist(patrol_dist)
  {
    pposBb = reg_entity_blackboard_var<Position>(entity, bb_name);
    entity.insert([&](Blackboard &bb, const Position &pos)
    {
      bb.set<Position>(pposBb, pos);
    });
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.insert([&](Action &a, const Position &pos)
    {
      Position patrolPos = bb.get<Position>(pposBb);
      if (dist(pos, patrolPos) > patrolDist)
        a.action = move_towards(pos, patrolPos);
      else
        a.action = GetRandomValue(EA_MOVE_START, EA_MOVE_END - 1); // do a random walk
    });
    return res;
  }
};

struct PatchUp : public BehNode
{
  float hpThreshold = 100.f;
  PatchUp(float threshold) : hpThreshold(threshold) {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &) override
  {
    BehResult res = BEH_SUCCESS;
    entity.insert([&](Action &a, Hitpoints &hp)
    {
      if (hp.hitpoints >= hpThreshold)
        return;
      res = BEH_RUNNING;
      a.action = EA_HEAL_SELF;
    });
    return res;
  }
};

struct ShowMsg : public BehNode
{
  //float threshold = 0.f;
  std::string msg;
  ShowMsg(std::string msg) : msg(std::move(msg)) {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &) override
  {
    BehResult res = BEH_SUCCESS;
    //entity.get([&](const Hitpoints &hp)
    //{
    //  res = hp.hitpoints < threshold ? BEH_SUCCESS : BEH_FAIL;
    //});
    printf("I am a %s\n", msg.c_str());
    return res;
  }
};



BehNode *sequence(const std::vector<BehNode*> &nodes)
{
  Sequence *seq = new Sequence;
  for (BehNode *node : nodes)
    seq->pushNode(node);
  return seq;
}

BehNode *selector(const std::vector<BehNode*> &nodes)
{
  Selector *sel = new Selector;
  for (BehNode *node : nodes)
    sel->pushNode(node);
  return sel;
}

BehNode *utility_selector(const std::vector<std::pair<BehNode*, utility_function>> &nodes)
{
  UtilitySelector *usel = new UtilitySelector;
  usel->utilityNodes = std::move(nodes);
  return usel;
}

BehNode *utility_selector_with_priority(const std::vector<std::pair<BehNode*, std::pair<utility_function, std::function<int(Blackboard&)>>>> &nodes)
{
  // (nodes - (utility-priority))
  UtilitySelectorWithPriority *usel = new UtilitySelectorWithPriority;
  usel->utilityNodes = std::move(nodes);
  return usel;
}

BehNode *utility_selector_with_small_random(const std::vector<std::pair<BehNode*, utility_function>> &nodes)
{
  UtilitySelectorWithSmallRandom *usel = new UtilitySelectorWithSmallRandom;
  usel->utilityNodes = std::move(nodes);
  return usel;
}

BehNode *move_to_entity(flecs::entity entity, const char *bb_name)
{
  return new MoveToEntity(entity, bb_name);
}

BehNode *is_low_hp(float thres)
{
  return new IsLowHp(thres);
}

BehNode *find_enemy(flecs::entity entity, float dist, const char *bb_name)
{
  return new FindEnemy(entity, dist, bb_name);
}

BehNode *find_named_item(flecs::entity entity, const char *bb_name, const std::string &nameOfItem)
{
  return new FindNamedItem(entity, bb_name, nameOfItem);
}

BehNode *find_item_with_hunger(flecs::entity entity, const char *bb_name)
{
  return new FindNearestItemWithTypeT<Hunger>(entity, bb_name);
}

BehNode *find_item_with_comfort(flecs::entity entity, const char *bb_name)
{
  return new FindNearestItemWithTypeT<Comfort>(entity, bb_name);
}

BehNode *find_item_with_social(flecs::entity entity, const char *bb_name)
{
  return new FindNearestItemWithTypeT<Social>(entity, bb_name);
}

BehNode *find_item_with_hygiene(flecs::entity entity, const char *bb_name)
{
  return new FindNearestItemWithTypeT<Hygiene>(entity, bb_name);
}

BehNode *find_item_with_fun(flecs::entity entity, const char *bb_name)
{
  return new FindNearestItemWithTypeT<Fun>(entity, bb_name);
}

BehNode *find_item_with_energy(flecs::entity entity, const char *bb_name)
{
  return new FindNearestItemWithTypeT<Energy>(entity, bb_name);
}

BehNode *find_item_with_bladder(flecs::entity entity, const char *bb_name)
{
  return new FindNearestItemWithTypeT<Bladder>(entity, bb_name);
}

BehNode *move_to_entity_with_map(flecs::entity entity, const char*bb_name)
{
  return new MoveToEntityWithMap(entity, bb_name);
}

BehNode *increase_smth(flecs::entity entity, const char *bb_name, const Action& action_at_connection,
        const std::string& charName)
{
  return new IncValue(entity, bb_name, action_at_connection, charName);
}

BehNode *increase_social_once(flecs::entity entity, float incValue)
{
  return new IncSocialOnce(entity, incValue);
}

BehNode *find_character(flecs::entity entity, const char*bb_name)
{
  return new FindCharacter(entity, bb_name);
}

BehNode *move_to_pos(flecs::entity entity, const char*bb_name)
{
  return new MoveToPos(entity, bb_name);
}

BehNode *find_home(flecs::entity entity, const char*bb_name)
{
  return new FindHome(entity, bb_name);
}

BehNode *flee(flecs::entity entity, const char *bb_name)
{
  return new Flee(entity, bb_name);
}

BehNode *patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
{
  return new Patrol(entity, patrol_dist, bb_name);
}

BehNode *patch_up(float thres)
{
  return new PatchUp(thres);
}

BehNode *show_msg(std::string msg)
{
  return new ShowMsg(msg);
}


