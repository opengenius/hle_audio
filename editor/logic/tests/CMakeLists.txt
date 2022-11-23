enable_testing()

################################
# Includes
################################

# include_directories(${PROJECT_SOURCE_DIR}/../source)

################################
# Libraries
################################

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        e2239ee6043f73722e7aa812a459f54a28552929 #release-1.11.0
  GIT_SHALLOW    ON
)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(BUILD_GTEST ON CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)
include(GoogleTest)

set(LIBS  
  gtest_main
  hlea_editor_logic
)

################################
# Unit Tests
################################

# Add test cpp files
add_executable(hlea_editor_logic_tests
  test_state.cpp
)

set_property(TARGET hlea_editor_logic_tests PROPERTY CXX_STANDARD 17)

target_link_libraries(hlea_editor_logic_tests ${LIBS})
gtest_discover_tests(hlea_editor_logic_tests)
