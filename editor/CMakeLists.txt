file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(StellarAliaEditor STATIC ${EDITOR_SOURCES})

target_link_libraries(StellarAliaEditor
    PUBLIC  StellarAliaRuntime StellarAliaImporter imgui
    PRIVATE $<$<TARGET_EXISTS:nfd>:nfd>
            $<$<TARGET_EXISTS:imguizmo>:imguizmo>
)

# editor/ is the include root for editor headers (e.g. #include "camera/EditorCamera.hpp").
# PUBLIC so targets linking StellarAliaEditor get it transitively.
target_include_directories(StellarAliaEditor
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
