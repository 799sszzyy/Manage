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

    add_executable(manage_bom_service_tests
        data/bom_service_tests.cpp
    )
    target_link_libraries(manage_bom_service_tests
        PRIVATE
            Manage::Data
            Qt6::Core
    )
    manage_apply_project_options(manage_bom_service_tests)
    add_test(NAME manage.data.bom_service COMMAND manage_bom_service_tests)

    add_executable(manage_bom_mysql_tests
        data/bom_mysql_integration_tests.cpp
    )
    target_link_libraries(manage_bom_mysql_tests
        PRIVATE
            Manage::Data
            Qt6::Core
            Qt6::Sql
    )
    manage_apply_project_options(manage_bom_mysql_tests)
    add_test(NAME manage.data.bom_mysql_integration
             COMMAND manage_bom_mysql_tests)
    add_test(NAME manage.data.bom_mysql_safety
             COMMAND manage_bom_mysql_tests)
    set_tests_properties(
        manage.data.bom_mysql_safety
        PROPERTIES
            WILL_FAIL TRUE
            ENVIRONMENT "MANAGE_TEST_MYSQL=1;MANAGE_DB_NAME=manage"
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

    add_executable(manage_bom_routes_tests
        server/bom_routes_tests.cpp
    )
    target_link_libraries(manage_bom_routes_tests
        PRIVATE
            Manage::Server
            Qt6::Core
            Qt6::Network
    )
    manage_apply_project_options(manage_bom_routes_tests)
    add_test(NAME manage.server.bom_routes COMMAND manage_bom_routes_tests)

    add_test(NAME manage.server.smoke COMMAND manage_server --smoke-test)
    add_test(NAME manage.desktop.smoke COMMAND manage_desktop --smoke-test)

    set_tests_properties(
        manage.data.migrations
        manage.data.bom_service
        manage.data.bom_mysql_integration
        manage.data.bom_mysql_safety
        manage.server.api
        manage.server.bom_routes
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
