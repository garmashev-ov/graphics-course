
add_executable(inflight_frames
  main.cpp
  App.cpp
)
# add_definitions(-DTRACY_ENABLE=ON)


target_link_libraries(inflight_frames
  PRIVATE glfw etna glm::glm wsi gui render_utils scene Tracy::TracyClient)


target_add_shaders(inflight_frames
  shaders/toy.frag
  shaders/toy.vert
  shaders/texture.frag
)