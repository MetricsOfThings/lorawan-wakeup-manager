add_library(vault_platform_host_mock STATIC
    src/platform_host_mock.c
)

target_include_directories(vault_platform_host_mock PUBLIC include)
target_link_libraries(vault_platform_host_mock PUBLIC vault_core)

# VAULT_LOG_ENABLED (core/CMakeLists.txt): a stderr-based vault_log()
# implementation, purely for a human watching test output locally --
# not needed by the test suite itself, but keeps the host target
# linkable if the flag is ever turned on here too.
if(VAULT_LOG_ENABLED)
    target_sources(vault_platform_host_mock PRIVATE src/platform_host_mock_log.c)
endif()
