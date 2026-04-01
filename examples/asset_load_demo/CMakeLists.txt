add_executable(AssetLoadDemo main.cpp)

target_link_libraries(AssetLoadDemo PRIVATE StellarAliaRuntime)
target_include_directories(AssetLoadDemo PRIVATE ${CMAKE_SOURCE_DIR}/src)
