cmake_minimum_required(VERSION 3.22)
project(octantpath-core LANGUAGES C CXX)

# コアはゲームにも Quest にも依存しない。MOD（../mod）は add_subdirectory で中に入れ、PC ではテストだけをビルドする
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(octantpath-core STATIC
  src/beatsaver.cpp
  src/connection.cpp
  src/contract_version.cpp
  src/host_client.cpp
  src/host_models.cpp
  src/ledger.cpp
  src/map_install.cpp
  src/panel_placement.cpp
  src/posix_transport.cpp
  src/presets.cpp
  src/recommendations.cpp
  src/recommender.cpp
  src/settings.cpp
  src/sha1.cpp
  src/status_text.cpp
  src/trials.cpp
  src/transport.cpp
)
# zip の展開（miniz）。同梱の C のソースを、そのまま中に入れる
add_library(octantpath-miniz STATIC third_party/miniz/miniz.c)
target_include_directories(octantpath-miniz PUBLIC third_party/miniz)
target_compile_options(octantpath-miniz PRIVATE -w)
set_target_properties(octantpath-miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(octantpath-core PUBLIC include)
target_link_libraries(octantpath-core PRIVATE octantpath-miniz)
target_include_directories(octantpath-core SYSTEM PUBLIC third_party)
set_target_properties(octantpath-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(OCTANTPATH_CORE_TESTS "Build the core tests" OFF)
if(OCTANTPATH_CORE_TESTS)
  enable_testing()
  add_executable(octantpath-core-tests
    tests/main.cpp
    tests/beatsaver_tests.cpp
    tests/connection_tests.cpp
    tests/map_install_tests.cpp
    tests/panel_placement_tests.cpp
    tests/contract_examples_tests.cpp
    tests/posix_transport_tests.cpp
    tests/recommendation_tests.cpp
    tests/trials_tests.cpp
  )
  target_link_libraries(octantpath-core-tests PRIVATE octantpath-core octantpath-miniz)
  target_compile_options(octantpath-core-tests PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
  # 契約3 の例（OctantSpecs の contracts/examples/mod-api/）。scripts/fetch-octantspecs.sh で取ってくる
  target_compile_definitions(octantpath-core-tests PRIVATE OCTANTSPECS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../.octantspecs")
  add_test(NAME octantpath-core-tests COMMAND octantpath-core-tests)
endif()
