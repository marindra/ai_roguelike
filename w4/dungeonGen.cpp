#include "dungeonGen.h"
#include "dungeonUtils.h"
#include <cstring> // memset
#include <cstdio> // printf
#include <random>
#include <chrono> // std::chrono
#include <functional> // std::bind
#include "ecsTypes.h"
#include "math.h"
#include <limits>

void gen_home_with_pos_and_size(char *tiles, size_t w_of_dungeon, const Position& pos, size_t width, size_t height)
{
  // Since the objects are stationary, I wasn't sure that with completely random generation
  // it would be possible to reach each of the objects, so I created houses.
  if (height == 0 || width == 0)
    return;

  for (size_t i = 0; i < width; ++i)
  {
    for (size_t j = 0; j < height; ++j)
    {
      tiles[(pos.y + j) * w_of_dungeon + pos.x + i] = dungeon::floor;
    }
  }
  return;
}

void gen_homes_in_dungeon(char *tiles, size_t w, size_t h, int *roomsCoords)
{
  //constexpr char wall = '#';
  //constexpr char flr = ' ';

  //memset(tiles, dungeon::floor, w * h);
  //gen_home_with_pos_and_size(tiles, w, Position{0, 0}, w, h);

  // generator
  unsigned seed = unsigned(std::chrono::system_clock::now().time_since_epoch().count() % std::numeric_limits<int>::max());
  std::default_random_engine seedGenerator(seed);
  std::default_random_engine widthGenerator(seedGenerator());
  std::default_random_engine heightGenerator(seedGenerator());

  // distributions
  std::uniform_int_distribution<size_t> widthDist(1, w-9);
  std::uniform_int_distribution<size_t> heightDist(1, h-9);
  auto rndWd = std::bind(widthDist, widthGenerator);
  auto rndHt = std::bind(heightDist, heightGenerator);

  constexpr size_t houseCount = 3;
  constexpr size_t generateCountForHouse = 10;
  std::vector<Position> startPos;
  for (size_t count = 0; count < houseCount; ++count)
  {
    // select random point on map
    bool needToRegenerate = false;
    size_t generateCount = 0;
    do {
      size_t x = rndWd();
      size_t y = rndHt();

      for (auto& oldPos : startPos)
      {
        if ((oldPos.x - (int)x < 11 && oldPos.y - (int)y < 11))// || (tiles[y * w + x] != dungeon::floor))
        {
          needToRegenerate = true;
          break;
        }
      }
      if (!needToRegenerate)
        startPos.push_back({int(x), int(y)});
      else
        ++generateCount;
    } while (needToRegenerate && generateCount <= generateCountForHouse);

    if (needToRegenerate)
    {
      startPos.clear();
      startPos.push_back({10, (int)rndHt()});
      startPos.push_back({19, (int)rndHt()});
      startPos.push_back({36, (int)rndHt()});
      break;
    }
  }

  for (int i = 0; i < 3; ++i)
  {
    roomsCoords[2 * i] = startPos[i].x;
    roomsCoords[2 * i + 1] = startPos[i].y;
  }

  // build houses
  for (int houseNum = 0; houseNum < houseCount; ++houseNum)
  {
    gen_home_with_pos_and_size(tiles, w, startPos[houseNum], 7, 7); // size without walls
  }

  for (size_t y = 0; y < h; ++y)
    printf("%.*s\n", int(w), tiles + y * w);
}

void gen_dungeon(char *tiles, size_t w, size_t h, int *roomsCoords)
{
  //constexpr char wall = '#';
  //constexpr char flr = ' ';

  memset(tiles, dungeon::wall, w * h);

  // generator
  unsigned seed = unsigned(std::chrono::system_clock::now().time_since_epoch().count() % std::numeric_limits<int>::max());
  std::default_random_engine seedGenerator(seed);
  std::default_random_engine widthGenerator(seedGenerator());
  std::default_random_engine heightGenerator(seedGenerator());
  std::default_random_engine dirGenerator(seedGenerator());

  // distributions
  std::uniform_int_distribution<size_t> widthDist(1, w-2);
  std::uniform_int_distribution<size_t> heightDist(1, h-2);
  std::uniform_int_distribution<size_t> dirDist(0, 3);
  auto rndWd = std::bind(widthDist, widthGenerator);
  auto rndHt = std::bind(heightDist, heightGenerator);
  auto rndDir = std::bind(dirDist, dirGenerator);

  const int dirs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};

  constexpr size_t numIter = 4;
  constexpr size_t maxExcavations = 200;
  std::vector<Position> startPos;
  for (size_t iter = 0; iter < numIter; ++iter)
  {
    // select random point on map
    size_t x = rndWd();
    size_t y = rndHt();
    startPos.push_back({int(x), int(y)});
    size_t numExcavations = 0;
    while (numExcavations < maxExcavations)
    {
      if (tiles[y * w + x] == dungeon::wall)
      {
        numExcavations++;
        tiles[y * w + x] = dungeon::floor;
      }
      // choose random dir
      size_t dir = rndDir(); // 0 - right, 1 - up, 2 - left, 3 - down
      int newX = std::min(std::max(int(x) + dirs[dir][0], 1), int(w) - 2);
      int newY = std::min(std::max(int(y) + dirs[dir][1], 1), int(h) - 2);
      x = size_t(newX);
      y = size_t(newY);
    }
  }

  gen_homes_in_dungeon(tiles, w, h, roomsCoords);
  for (int i = 0; i < 3; ++i)
  {
    startPos.push_back(Position{roomsCoords[2*i], roomsCoords[2*i+1]});
  }

  // construct a path from start pos to all other start poses
  for (const Position &spos : startPos)
    for (const Position &epos : startPos)
    {
      Position pos = spos;
      while (dist_sq(pos, epos) > 0.f)
      {
        const Position delta = epos - pos;
        if (abs(delta.x) > abs(delta.y))
          pos.x += delta.x > 0 ? 1 : -1;
        else
          pos.y += delta.y > 0 ? 1 : -1;
        tiles[size_t(pos.y) * w + size_t(pos.x)] = dungeon::floor;
      }
    }

  for (size_t y = 0; y < h; ++y)
    printf("%.*s\n", int(w), tiles + y * w);
}