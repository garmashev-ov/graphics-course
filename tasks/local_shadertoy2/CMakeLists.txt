
add_executable(local_shadertoy2
  main.cpp
  App.cpp
)

add_compile_options("/fsanitize=address -Wall -Wextra -Werror -Wpedantic")

target_link_libraries(local_shadertoy2
  PRIVATE glfw etna glm::glm wsi gui render_utils scene)

target_add_shaders(local_shadertoy2
  shaders/toy.frag
  shaders/toy.vert
  shaders/texture.frag
)