add_library(manage_desktop_lib STATIC
    include/manage/desktop/api_client.h
    include/manage/desktop/bom_drag_widgets.h
    include/manage/desktop/bom_quote_widget.h
    include/manage/desktop/catalog_widget.h
    include/manage/desktop/excel_tools_widget.h
    include/manage/desktop/quote_management_widget.h
    include/manage/desktop/user_management_widget.h
    include/manage/desktop/statistics_widget.h
    src/api_client.cpp
    src/bom_drag_widgets.cpp
    src/bom_quote_widget.cpp
    src/catalog_widget.cpp
    src/excel_tools_widget.cpp
    src/quote_management_widget.cpp
    src/user_management_widget.cpp
    src/statistics_widget.cpp
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
        Manage::Excel
)

manage_apply_project_options(manage_desktop_lib)

qt_add_executable(manage_desktop WIN32
    app/main.cpp
)

target_link_libraries(manage_desktop PRIVATE Manage::Desktop)
target_compile_definitions(manage_desktop PRIVATE
    MANAGE_VERSION="${PROJECT_VERSION}"
)
manage_apply_project_options(manage_desktop)

set_target_properties(manage_desktop PROPERTIES
    OUTPUT_NAME "manage-desktop"
)
