enable_testing()

################################
# Includes
################################

# include_directories(${PROJECT_SOURCE_DIR}/../source)

################################
# Libraries
################################

FetchContent_Declare(googletest
  # 1.15.2
  URL https://github.com/google/googletest/archive/b514bdc898e2951020cbdca1304b75f5950d1f59.zip
)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(BUILD_GTEST ON CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)
include(GoogleTest)

set(LIBS  
  GTest::gtest_main
  hlea_editor_logic
)

################################
# Unit Tests
################################

# Add test cpp files
add_executable(hlea_editor_logic_tests
  test_state.cpp
)

target_link_libraries(hlea_editor_logic_tests ${LIBS})
gtest_discover_tests(hlea_editor_logic_tests)
