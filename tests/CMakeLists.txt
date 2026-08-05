add_executable(manage_domain_tests
    domain/quote_calculator_tests.cpp
)

target_link_libraries(manage_domain_tests PRIVATE Manage::Domain)
manage_apply_project_options(manage_domain_tests)

add_test(NAME manage.domain.quote_calculator COMMAND manage_domain_tests)

if(TARGET Manage::Data)
    add_executable(manage_data_tests
        data/migration_runner_tests.cpp
    )

    target_link_libraries(manage_data_tests
        PRIVATE
            Manage::Data
            Qt6::Core
            Qt6::Sql
    )
    manage_apply_project_options(manage_data_tests)

    add_test(NAME manage.data.migrations COMMAND manage_data_tests)

    add_executable(manage_catalog_service_tests
        data/catalog_service_tests.cpp
    )
    target_include_directories(manage_catalog_service_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(manage_catalog_service_tests PRIVATE Manage::Data Qt6::Core)
    manage_apply_project_options(manage_catalog_service_tests)
    add_test(NAME manage.data.catalog_service COMMAND manage_catalog_service_tests)

    add_executable(manage_catalog_mysql_tests
        data/catalog_repository_mysql_tests.cpp
    )
    target_link_libraries(
        manage_catalog_mysql_tests
        PRIVATE
            Manage::Data
            Qt6::Core
            Qt6::Sql
    )
    manage_apply_project_options(manage_catalog_mysql_tests)
    add_test(
        NAME manage.data.catalog_repository_mysql
        COMMAND manage_catalog_mysql_tests
    )
endif()

if(TARGET Manage::Server)
    add_executable(manage_server_tests
        server/api_server_tests.cpp
    )

    target_link_libraries(manage_server_tests
        PRIVATE
            Manage::Server
            Qt6::Core
            Qt6::Network
    )
    manage_apply_project_options(manage_server_tests)

    add_test(NAME manage.server.api COMMAND manage_server_tests)

    add_executable(manage_catalog_routes_tests
        server/catalog_routes_tests.cpp
    )
    target_include_directories(manage_catalog_routes_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(
        manage_catalog_routes_tests
        PRIVATE
            Manage::Server
            Qt6::Core
            Qt6::Network
    )
    manage_apply_project_options(manage_catalog_routes_tests)
    add_test(NAME manage.server.catalog_routes COMMAND manage_catalog_routes_tests)
    add_test(NAME manage.server.smoke COMMAND manage_server --smoke-test)
    add_test(NAME manage.desktop.smoke COMMAND manage_desktop --smoke-test)

    set_tests_properties(
        manage.data.migrations
        manage.server.api
        manage.server.catalog_routes
        manage.data.catalog_service
        manage.data.catalog_repository_mysql
        manage.server.smoke
        PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
    set_tests_properties(
        manage.desktop.smoke
        PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen"
    )
endif()
