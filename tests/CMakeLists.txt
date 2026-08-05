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
    target_include_directories(manage_server_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    manage_apply_project_options(manage_server_tests)

    add_test(NAME manage.server.api COMMAND manage_server_tests)
    add_test(NAME manage.server.smoke COMMAND manage_server --smoke-test)
    add_test(NAME manage.desktop.smoke COMMAND manage_desktop --smoke-test)

    set_tests_properties(
        manage.data.migrations
        manage.auth.service
        manage.server.api
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
