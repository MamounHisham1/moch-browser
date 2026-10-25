qt_add_executable(tst_security
    unit/tst_security.cpp
    ../src/security/contentsanitizer.cpp
    ../src/security/riskclassifier.cpp
    ../src/security/secretredactor.cpp
)

target_include_directories(tst_security PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(tst_security PRIVATE Qt6::Core Qt6::Test)

add_test(NAME tst_security COMMAND tst_security)
set_tests_properties(tst_security PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
