# vip_healthshot — Plugify C++ plugin: port of the Pisex VIP module "VIP_Healthshot".
#
# Pure s2sdk: no s2guard and no pisexcompat (docs/PLUGIN_STANDARD.md §7). Every safety rule the
# module used to get from pisexcompat lives in src/: the map gate and the slot/controller/pawn
# checks (plugin.cpp), the map-gated schema-offset cache (schema_cache.hpp, PITFALLS C8) and the
# plugin's own read-only KV1 config reader (kv1.hpp) that replaced pisex::KeyValues.
#
#   plugify-vip_healthshot  the plugin shared library (entry "bin/vip_healthshot")
#   vip_healthshot_tests    engine-free unit tests: the round condition, the healthshot top-up
#                           count, the KV1 reader on the production vip_healthshot.ini and the
#                           schema-offset cache policy (build/tests/vip_healthshot_tests)

set(VIP_HEALTHSHOT_VERSION "1.1.0")

# Binding globals the plugify module fills from the plugins declared as dependencies:
#   __s2sdk_*   (s2sdk_bindings)
#   __vip_*     (vip consumer bindings, generated into plugins/vip/include/external/plugify/include)
# In the monorepo plugins/vip is added first ("vip" < "vip_healthshot"), so vip_bindings already
# exists; a standalone exported repo only carries plugins/vip/include/ headers + vip.cpp and has
# no plugins/vip/CMakeLists.txt - define vip_bindings here when absent.
if(NOT TARGET vip_bindings)
    add_library(vip_bindings OBJECT
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include/vip/vip.cpp")
    target_include_directories(vip_bindings SYSTEM PUBLIC
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
            "${CMAKE_SOURCE_DIR}/sdk/include")
    target_compile_definitions(vip_bindings PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
endif()

add_library(plugify-vip_healthshot SHARED
        src/plugin.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
        $<TARGET_OBJECTS:vip_bindings>
)
target_include_directories(plugify-vip_healthshot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated vip/s2sdk/plg headers must not fail -Werror on our sources.
target_include_directories(plugify-vip_healthshot SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/plugins/vip/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(plugify-vip_healthshot PRIVATE
        plugify_sdk
        -static-libstdc++ -static-libgcc
)
target_compile_options(plugify-vip_healthshot PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(plugify-vip_healthshot PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="vip_healthshot"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
)
set_target_properties(plugify-vip_healthshot PROPERTIES OUTPUT_NAME vip_healthshot)

if(UNIX AND NOT APPLE)
    target_link_options(plugify-vip_healthshot PRIVATE
            "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/sym/version_script.lds")
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/vip_healthshot.pplugin.in"
        "${CMAKE_CURRENT_BINARY_DIR}/vip_healthshot.pplugin"
)

# --- tests --------------------------------------------------------------------
# Engine-free logic tests, no s2sdk, no vip bindings and no pisexcompat: the pure helpers are
# header-only (round_rule.hpp, kv1.hpp, schema_cache.hpp). The KV1 half reads the production-shaped
# vip_healthshot.ini from tests/data. Run by ./test.sh (build/tests).
add_executable(vip_healthshot_tests tests/vip_healthshot_tests.cpp)
target_include_directories(vip_healthshot_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(vip_healthshot_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(vip_healthshot_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        VIP_HEALTHSHOT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
)
target_link_libraries(vip_healthshot_tests PRIVATE -static-libstdc++ -static-libgcc)
set_target_properties(vip_healthshot_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
