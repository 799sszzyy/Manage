add_library(manage_domain STATIC
    src/lead_time_calculator.cpp
    src/quote_calculator.cpp
)

add_library(Manage::Domain ALIAS manage_domain)

target_include_directories(manage_domain
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(manage_domain PUBLIC cxx_std_17)
manage_apply_project_options(manage_domain)
