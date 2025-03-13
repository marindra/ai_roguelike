#pragma once
#include <cstddef> // size_t

void gen_dungeon(char *tiles, size_t w, size_t h, int *roomsCoords);
void gen_homes_in_dungeon(char *tiles, size_t w, size_t y, int *roomsCoords);
