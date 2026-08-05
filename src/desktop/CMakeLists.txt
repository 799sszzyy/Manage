add_library(manage_desktop_lib STATIC
    include/manage/desktop/api_client.h
    include/manage/desktop/bom_quote_widget.h
    src/api_client.cpp
    src/bom_quote_widget.cpp
    src/main_window.cpp
)

add_library(Manage::Desktop ALIAS manage_desktop_lib)

target_include_directories(manage_desktop_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(manage_desktop_lib
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::Widgets
)

manage_apply_project_options(manage_desktop_lib)

qt_add_executable(manage_desktop WIN32
    app/main.cpp
)

target_link_libraries(manage_desktop PRIVATE Manage::Desktop)
manage_apply_project_options(manage_desktop)

set_target_properties(manage_desktop PROPERTIES
    OUTPUT_NAME "manage-desktop"
)
