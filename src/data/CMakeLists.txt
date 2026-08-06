add_library(manage_data STATIC
    src/bom_service.cpp
    src/catalog_repository.cpp
    src/catalog_service.cpp
    src/database_config.cpp
    src/database_connection.cpp
    src/migration_runner.cpp
    src/material_batch_service.cpp
    src/mysql_bom_repository.cpp
    src/mysql_quote_lifecycle.cpp
    src/mysql_statistics_repository.cpp
    src/mysql_user_repository.cpp
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
        Manage::Auth
        Manage::Domain
)

qt_add_resources(manage_data "manage_migrations"
    PREFIX "/manage/migrations"
    BASE "${PROJECT_SOURCE_DIR}/db-migrations"
    FILES
        "${PROJECT_SOURCE_DIR}/db-migrations/001_initial_schema.sql"
        "${PROJECT_SOURCE_DIR}/db-migrations/002_quote_bom_template.sql"
        "${PROJECT_SOURCE_DIR}/db-migrations/003_quote_bom_quantity.sql"
        "${PROJECT_SOURCE_DIR}/db-migrations/004_material_supplier_pricing.sql"
)

manage_apply_project_options(manage_data)
