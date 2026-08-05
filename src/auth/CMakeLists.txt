add_library(manage_auth STATIC
    src/auth_service.cpp
    src/password_hasher.cpp
)

add_library(Manage::Auth ALIAS manage_auth)

target_include_directories(manage_auth
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(manage_auth PUBLIC Qt6::Core)
manage_apply_project_options(manage_auth)
