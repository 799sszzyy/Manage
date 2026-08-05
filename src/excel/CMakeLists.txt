add_library(manage_excel STATIC
    include/manage/excel/workbook_models.h
    include/manage/excel/workbook_service.h
    src/workbook_service.cpp
)

add_library(Manage::Excel ALIAS manage_excel)

target_include_directories(manage_excel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(manage_excel
    PUBLIC
        Manage::Data
        Qt6::Core
        QXlsx::QXlsx
)

manage_apply_project_options(manage_excel)
