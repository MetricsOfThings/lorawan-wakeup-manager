add_library(vault_platform_host_mock STATIC
    src/platform_host_mock.c
)

target_include_directories(vault_platform_host_mock PUBLIC include)
target_link_libraries(vault_platform_host_mock PUBLIC vault_core)
