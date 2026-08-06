add_library(manage_server_lib STATIC
    src/api_server.cpp
    src/bom_routes.cpp
    src/catalog_routes.cpp
    src/http_authorization.cpp
    src/material_batch_routes.cpp
    src/process_step_routes.cpp
    src/quote_routes.cpp
    src/task_routes.cpp
    src/user_routes.cpp
    src/statistics_routes.cpp
)

add_library(Manage::Server ALIAS manage_server_lib)

target_include_directories(manage_server_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(manage_server_lib
    PUBLIC
        Manage::Data
        Manage::Domain
        Manage::Auth
        Qt6::Core
        Qt6::HttpServer
        Qt6::Network
)

manage_apply_project_options(manage_server_lib)

qt_add_executable(manage_server
    app/main.cpp
)

target_link_libraries(manage_server PRIVATE Manage::Server)
target_compile_definitions(manage_server PRIVATE
    MANAGE_VERSION="${PROJECT_VERSION}"
)
manage_apply_project_options(manage_server)

set_target_properties(manage_server PROPERTIES
    OUTPUT_NAME "manage-server"
)
