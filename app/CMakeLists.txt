# The frontend: an SDL2 window, or --headless for test ROMs and CI.
# SDL2 is linked into the executable only; the core never depends on it.
find_package(SDL2 REQUIRED CONFIG)

# Frontend code with no SDL in it, so the unit tests can link it too.
add_library(frontend STATIC
  save_file.cpp
  state_file.cpp
)
target_link_libraries(frontend PUBLIC gb::core PRIVATE gb_warnings)

add_executable(gameboy-emu
  main.cpp
  sdl_frontend.cpp
)

# main() is our own; SDL must not replace it with SDL_main.
target_compile_definitions(gameboy-emu PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(gameboy-emu PRIVATE frontend gb::core SDL2::SDL2 gb_warnings)
