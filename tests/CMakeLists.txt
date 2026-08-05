add_executable(manage_domain_tests
    domain/quote_calculator_tests.cpp
)

target_link_libraries(manage_domain_tests PRIVATE Manage::Domain)
manage_apply_project_options(manage_domain_tests)

add_test(NAME manage.domain.quote_calculator COMMAND manage_domain_tests)

if(TARGET Manage::Auth)
    add_executable(manage_auth_tests
        auth/auth_service_tests.cpp
    )
    target_include_directories(manage_auth_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(manage_auth_tests PRIVATE Manage::Auth Qt6::Core)
    manage_apply_project_options(manage_auth_tests)
    add_test(NAME manage.auth.service COMMAND manage_auth_tests)
endif()

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
    add_executable(manage_backend_e2e
        e2e/backend_e2e_workflow.cpp
    )
    target_link_libraries(
        manage_backend_e2e
        PRIVATE
            Qt6::Core
            Qt6::Network
    )
    manage_apply_project_options(manage_backend_e2e)
    add_test(
        NAME manage.backend_e2e.self_test
        COMMAND manage_backend_e2e --self-test
    )

    add_executable(manage_server_tests
        server/api_server_tests.cpp
    )

    target_link_libraries(manage_server_tests
        PRIVATE
            Manage::Server
            Qt6::Core
            Qt6::Network
    )
    target_include_directories(manage_server_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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

    add_executable(manage_bom_routes_tests
        server/bom_routes_tests.cpp
    )
    target_include_directories(
        manage_bom_routes_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
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
        manage.auth.service
        manage.data.catalog_service
        manage.data.catalog_repository_mysql
        manage.data.bom_service
        manage.data.bom_mysql_integration
        manage.data.bom_mysql_safety
        manage.server.api
        manage.server.catalog_routes
        manage.server.bom_routes
        manage.server.smoke
        manage.backend_e2e.self_test
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

if(TARGET Manage::Desktop)
    add_executable(manage_desktop_api_client_tests
        desktop/api_client_tests.cpp
    )
    target_link_libraries(
        manage_desktop_api_client_tests
        PRIVATE
            Manage::Desktop
            Qt6::Core
            Qt6::HttpServer
            Qt6::Network
    )
    manage_apply_project_options(manage_desktop_api_client_tests)
    add_test(
        NAME manage.desktop.api_client
        COMMAND manage_desktop_api_client_tests
    )
    set_tests_properties(
        manage.desktop.api_client
        PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )

    add_executable(manage_desktop_login_flow_tests
        desktop/main_window_login_flow_tests.cpp
    )
    target_link_libraries(
        manage_desktop_login_flow_tests
        PRIVATE
            Manage::Desktop
            Qt6::Core
            Qt6::HttpServer
            Qt6::Network
            Qt6::Widgets
    )
    manage_apply_project_options(manage_desktop_login_flow_tests)
    add_test(
        NAME manage.desktop.login_flow
        COMMAND manage_desktop_login_flow_tests
    )
    set_tests_properties(
        manage.desktop.login_flow
        PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen"
    )

    add_executable(manage_desktop_catalog_widget_tests
        desktop/catalog_widget_tests.cpp
    )
    target_link_libraries(
        manage_desktop_catalog_widget_tests
        PRIVATE
            Manage::Desktop
            Qt6::Core
            Qt6::HttpServer
            Qt6::Network
            Qt6::Widgets
    )
    manage_apply_project_options(manage_desktop_catalog_widget_tests)
    add_test(
        NAME manage.desktop.catalog_widget
        COMMAND manage_desktop_catalog_widget_tests
    )
    set_tests_properties(
        manage.desktop.catalog_widget
        PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen"
    )

    add_executable(manage_bom_quote_widget_tests
        desktop/bom_quote_widget_tests.cpp
    )
    target_link_libraries(
        manage_bom_quote_widget_tests
        PRIVATE
            Manage::Desktop
            Qt6::Core
            Qt6::HttpServer
            Qt6::Network
            Qt6::Widgets
    )
    manage_apply_project_options(manage_bom_quote_widget_tests)
    add_test(
        NAME manage.desktop.bom_quote_widget
        COMMAND manage_bom_quote_widget_tests
    )
    set_tests_properties(
        manage.desktop.bom_quote_widget
        PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen"
    )
endif()
