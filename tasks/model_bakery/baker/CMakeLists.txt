#add_compile_options(-Wall -Wextra -Wpedantic -Werror)
include(${PROJECT_SOURCE_DIR}/cmake/common.cmake)

add_executable(model_bakery_baker
  main.cpp
)

target_link_libraries(model_bakery_baker
  PRIVATE tinygltf glm::glm etna)
