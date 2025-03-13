//
#include "raylib.h"
#include <flecs.h>
#include <algorithm>
#include "ecsTypes.h"
#include "roguelike.h"
#include "dungeonGen.h"

static void update_camera(Camera2D &cam, flecs::world &ecs)
{
  static auto playerQuery = ecs.query<const Position, const IsPlayer>();
  static auto mouseQuery = ecs.query<MousePos>();

  playerQuery.each([&](const Position &pos, const IsPlayer &)
  {
    cam.target.x += (pos.x * tile_size - cam.target.x) * 0.1f;
    cam.target.y += (pos.y * tile_size - cam.target.y) * 0.1f;

    mouseQuery.each([&](MousePos &mpos)
    {
      mpos.x = pos.x + floor((GetMouseX() - cam.offset.x) / (tile_size * cam.zoom));
      mpos.y = pos.y + floor((GetMouseY() - cam.offset.y) / (tile_size * cam.zoom));
    });
  });
}

int main(int /*argc*/, const char ** /*argv*/)
{
  int width = 1920;
  int height = 1080;
  InitWindow(width, height, "w3 AI MIPT");

  const int scrWidth = GetMonitorWidth(0);
  const int scrHeight = GetMonitorHeight(0);
  if (scrWidth < width || scrHeight < height)
  {
    width = std::min(scrWidth, width);
    height = std::min(scrHeight - 150, height);
    SetWindowSize(width, height);
  }

  int *roomsCoord = new int[6];
  flecs::world ecs;
  {
    constexpr size_t dungWidth = 50;
    constexpr size_t dungHeight = 50;
    char *tiles = new char[dungWidth * dungHeight];
    gen_dungeon(tiles, dungWidth, dungHeight, roomsCoord);
    init_dungeon(ecs, tiles, dungWidth, dungHeight);
  }
  init_roguelike(ecs, roomsCoord);

  Camera2D camera = { {0, 0}, {0, 0}, 0.f, 1.f };
  camera.target = Vector2{ 0.f, 0.f };
  camera.offset = Vector2{ width * 0.5f, height * 0.5f };
  camera.rotation = 0.f;
  camera.zoom = 0.125f;

  SetTargetFPS(60);               // Set our game to run at 60 frames-per-second
  while (!WindowShouldClose())
  {
    process_turn(ecs);
    update_camera(camera, ecs);

    BeginDrawing();
      ClearBackground(BLACK);
      BeginMode2D(camera);
        ecs.progress();
      EndMode2D();
      print_stats(ecs);
      // Advance to next frame. Process submitted rendering primitives.
    EndDrawing();
  }

  CloseWindow();

  return 0;
}
