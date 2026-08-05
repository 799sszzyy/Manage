add_library(manage_data STATIC
    src/catalog_repository.cpp
    src/catalog_service.cpp
    src/database_config.cpp
    src/database_connection.cpp
    src/migration_runner.cpp
)

add_library(Manage::Data ALIAS manage_data)

target_include_directories(manage_data
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(manage_data
    PUBLIC
        Qt6::Core
        Qt6::Sql
)

qt_add_resources(manage_data "manage_migrations"
    PREFIX "/manage/migrations"
    BASE "${PROJECT_SOURCE_DIR}/db-migrations"
    FILES
        "${PROJECT_SOURCE_DIR}/db-migrations/001_initial_schema.sql"
)

manage_apply_project_options(manage_data)
