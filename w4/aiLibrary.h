#pragma once

#include <functional>
#include "stateMachine.h"
#include "behaviourTree.h"

// states
State *create_attack_enemy_state();
State *create_move_to_enemy_state();
State *create_flee_from_enemy_state();
State *create_patrol_state(float patrol_dist);
State *create_nop_state();

// transitions
StateTransition *create_enemy_available_transition(float dist);
StateTransition *create_enemy_reachable_transition();
StateTransition *create_hitpoints_less_than_transition(float thres);
StateTransition *create_negate_transition(StateTransition *in);
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs);

using utility_function = std::function<float(Blackboard&)>;

BehNode *sequence(const std::vector<BehNode*> &nodes);
BehNode *selector(const std::vector<BehNode*> &nodes);
BehNode *utility_selector(const std::vector<std::pair<BehNode*, utility_function>> &nodes);
BehNode *utility_selector_with_small_random(const std::vector<std::pair<BehNode*, utility_function>> &nodes);
BehNode *utility_selector_with_priority(const std::vector<std::pair<BehNode*, std::pair<utility_function, std::function<int(Blackboard&)>>>> &nodes);

BehNode *move_to_entity(flecs::entity entity, const char *bb_name);
BehNode *is_low_hp(float thres);
BehNode *find_enemy(flecs::entity entity, float dist, const char *bb_name);
BehNode *flee(flecs::entity entity, const char *bb_name);
BehNode *patrol(flecs::entity entity, float patrol_dist, const char *bb_name);
BehNode *patch_up(float thres);

BehNode *show_msg(std::string msg);
BehNode *find_item_with_hunger(flecs::entity entity, const char *bb_name);
BehNode *find_item_with_comfort(flecs::entity entity, const char *bb_name);
BehNode *find_item_with_social(flecs::entity entity, const char *bb_name);
BehNode *find_item_with_hygiene(flecs::entity entity, const char *bb_name);
BehNode *find_item_with_fun(flecs::entity entity, const char *bb_name);
BehNode *find_item_with_energy(flecs::entity entity, const char *bb_name);
BehNode *find_item_with_bladder(flecs::entity entity, const char *bb_name);
BehNode *move_to_entity_with_map(flecs::entity entity, const char*bb_name);
BehNode *find_named_item(flecs::entity entity, const char *bb_name, const std::string &nameOfItem);
BehNode *increase_smth(flecs::entity entity, const char *bb_name, const Action& action_forInc, const std::string& charName);
BehNode *increase_social_once(flecs::entity entity, float incValue);

BehNode *find_character(flecs::entity entity, const char*bb_name);
BehNode *move_to_pos(flecs::entity entity, const char*bb_name);
BehNode *find_home(flecs::entity entity, const char*bb_name);