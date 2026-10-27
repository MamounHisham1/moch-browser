qt_add_executable(tst_provider
    tst_provider.cpp
    mockopenaiserver.cpp mockopenaiserver.hpp
    ../src/ai/provider.cpp ../src/ai/provider.hpp
    ../src/ai/openaicompatprovider.cpp ../src/ai/openaicompatprovider.hpp
    ../src/storage/settings.cpp ../src/storage/settings.hpp
)

target_include_directories(tst_provider PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(tst_provider PRIVATE Qt6::Core Qt6::Network Qt6::Test)

add_test(NAME tst_provider COMMAND tst_provider)
set_tests_properties(tst_provider PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(tst_security
    unit/tst_security.cpp
    ../src/security/contentsanitizer.cpp
    ../src/security/riskclassifier.cpp
    ../src/security/secretbox.cpp ../src/security/secretbox.hpp
    ../src/security/secretredactor.cpp
    ../src/security/permissionpolicy.cpp
)

target_include_directories(tst_security PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(tst_security PRIVATE Qt6::Core Qt6::Test)

add_test(NAME tst_security COMMAND tst_security)
set_tests_properties(tst_security PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(tst_browsercore
    tst_browsercore.cpp
    ../src/browser/bookmarkstore.cpp ../src/browser/bookmarkstore.hpp
    ../src/browser/historystore.cpp ../src/browser/historystore.hpp
    ../src/browser/tabmanager.cpp ../src/browser/tabmanager.hpp
    ../src/browser/urlresolver.cpp ../src/browser/urlresolver.hpp
    ../src/browser/webview.cpp ../src/browser/webview.hpp
    ../src/browser/newtabbridge.cpp ../src/browser/newtabbridge.hpp
    ../src/browser/setupbridge.cpp ../src/browser/setupbridge.hpp
    ../src/browser/openrouterauth.cpp ../src/browser/openrouterauth.hpp
    ../src/ai/modelcatalog.cpp ../src/ai/modelcatalog.hpp
    ../src/storage/database.cpp ../src/storage/database.hpp
    ../src/storage/settings.cpp ../src/storage/settings.hpp
)

target_include_directories(tst_browsercore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(tst_browsercore PRIVATE
    Qt6::Core Qt6::Widgets Qt6::WebEngineWidgets Qt6::Sql Qt6::WebChannel Qt6::Test
)

add_test(NAME tst_browsercore COMMAND tst_browsercore)
set_tests_properties(tst_browsercore PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(tst_agent
    tst_agent.cpp
    mockopenaiserver.cpp mockopenaiserver.hpp
    ../src/agent/agentservice.cpp ../src/agent/agentservice.hpp
    ../src/agent/approvalgate.cpp ../src/agent/approvalgate.hpp
    ../src/ai/provider.cpp ../src/ai/provider.hpp
    ../src/ai/openaicompatprovider.cpp ../src/ai/openaicompatprovider.hpp
    ../src/browser/bookmarkstore.cpp ../src/browser/bookmarkstore.hpp
    ../src/browser/historystore.cpp ../src/browser/historystore.hpp
    ../src/browser/tabmanager.cpp ../src/browser/tabmanager.hpp
    ../src/browser/newtabbridge.cpp ../src/browser/newtabbridge.hpp
    ../src/browser/setupbridge.cpp ../src/browser/setupbridge.hpp
    ../src/browser/openrouterauth.cpp ../src/browser/openrouterauth.hpp
    ../src/ai/modelcatalog.cpp ../src/ai/modelcatalog.hpp
    ../src/browser/urlresolver.cpp ../src/browser/urlresolver.hpp
    ../src/browser/webview.cpp ../src/browser/webview.hpp
    ../src/security/contentsanitizer.cpp
    ../src/security/permissionpolicy.cpp
    ../src/security/riskclassifier.cpp
    ../src/security/secretredactor.cpp
    ../src/storage/database.cpp
    ../src/storage/sessionsstore.cpp ../src/storage/sessionsstore.hpp
    ../src/storage/settings.cpp
    ../src/tools/tool.cpp
    ../src/tools/secretboxtools.cpp
    ../src/tools/toolregistry.cpp
    ../src/tools/pagetools.cpp
    ../src/security/contentsanitizer.cpp
    ../src/security/riskclassifier.cpp
    ../src/security/secretredactor.cpp
    ../src/security/secretbox.cpp ../src/security/secretbox.hpp
    ../src/security/permissionpolicy.cpp
)

target_include_directories(tst_agent PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(tst_agent PRIVATE
    Qt6::Core Qt6::Widgets Qt6::WebEngineWidgets Qt6::Network Qt6::Sql Qt6::WebChannel Qt6::Test
)

add_test(NAME tst_agent COMMAND tst_agent)
set_tests_properties(tst_agent PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(tst_setup
    tst_setup.cpp
    ../src/ai/modelcatalog.cpp ../src/ai/modelcatalog.hpp
    ../src/browser/openrouterauth.cpp ../src/browser/openrouterauth.hpp
    ../src/browser/setupbridge.cpp ../src/browser/setupbridge.hpp
    ../src/browser/webview.cpp ../src/browser/webview.hpp
    ../src/storage/settings.cpp ../src/storage/settings.hpp
)

target_include_directories(tst_setup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(tst_setup PRIVATE
    Qt6::Core Qt6::Widgets Qt6::WebEngineWidgets Qt6::Network Qt6::Test
)

add_test(NAME tst_setup COMMAND tst_setup)
set_tests_properties(tst_setup PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(tst_navtool
    tst_navtool.cpp
    ../src/browser/bookmarkstore.cpp ../src/browser/bookmarkstore.hpp
    ../src/browser/historystore.cpp ../src/browser/historystore.hpp
    ../src/browser/tabmanager.cpp ../src/browser/tabmanager.hpp
    ../src/browser/newtabbridge.cpp ../src/browser/newtabbridge.hpp
    ../src/browser/setupbridge.cpp ../src/browser/setupbridge.hpp
    ../src/browser/openrouterauth.cpp ../src/browser/openrouterauth.hpp
    ../src/ai/modelcatalog.cpp ../src/ai/modelcatalog.hpp
    ../src/browser/urlresolver.cpp ../src/browser/urlresolver.hpp
    ../src/browser/webview.cpp ../src/browser/webview.hpp
    ../src/storage/database.cpp ../src/storage/database.hpp
    ../src/storage/settings.cpp ../src/storage/settings.hpp
    ../src/tools/tool.cpp
    ../src/tools/secretboxtools.cpp
    ../src/tools/toolregistry.cpp
    ../src/tools/pagetools.cpp
    ../src/security/contentsanitizer.cpp
    ../src/security/riskclassifier.cpp
    ../src/security/secretredactor.cpp
    ../src/security/secretbox.cpp ../src/security/secretbox.hpp
    ../src/security/permissionpolicy.cpp
)

target_include_directories(tst_navtool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(tst_navtool PRIVATE
    Qt6::Core Qt6::Widgets Qt6::WebEngineWidgets Qt6::Network Qt6::Sql Qt6::WebChannel Qt6::Test
)

add_test(NAME tst_navtool COMMAND tst_navtool)
set_tests_properties(tst_navtool PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 90
)

qt_add_executable(tst_taskdetect
    tst_taskdetect.cpp
    ../src/agent/taskdetect.cpp ../src/agent/taskdetect.hpp
)

target_include_directories(tst_taskdetect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(tst_taskdetect PRIVATE Qt6::Core Qt6::Test)

add_test(NAME tst_taskdetect COMMAND tst_taskdetect)
set_tests_properties(tst_taskdetect PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(tst_sessions
    tst_sessions.cpp
    ../src/storage/sessionsstore.cpp ../src/storage/sessionsstore.hpp
    ../src/storage/database.cpp ../src/storage/database.hpp
    ../src/storage/settings.cpp ../src/storage/settings.hpp
)

target_include_directories(tst_sessions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(tst_sessions PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

add_test(NAME tst_sessions COMMAND tst_sessions)
set_tests_properties(tst_sessions PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(tst_secretbox
    tst_secretbox.cpp
    ../src/security/secretbox.cpp ../src/security/secretbox.hpp
    ../src/tools/secretboxtools.cpp
    ../src/tools/tool.cpp ../src/tools/tool.hpp
    ../src/tools/toolregistry.cpp ../src/tools/toolregistry.hpp
    ../src/tools/pagetools.cpp ../src/tools/pagetools.hpp
    ../src/browser/bookmarkstore.cpp ../src/browser/bookmarkstore.hpp
    ../src/browser/historystore.cpp ../src/browser/historystore.hpp
    ../src/browser/tabmanager.cpp ../src/browser/tabmanager.hpp
    ../src/browser/newtabbridge.cpp ../src/browser/newtabbridge.hpp
    ../src/browser/setupbridge.cpp ../src/browser/setupbridge.hpp
    ../src/browser/openrouterauth.cpp ../src/browser/openrouterauth.hpp
    ../src/browser/urlresolver.cpp ../src/browser/urlresolver.hpp
    ../src/browser/webview.cpp ../src/browser/webview.hpp
    ../src/ai/modelcatalog.cpp ../src/ai/modelcatalog.hpp
    ../src/storage/database.cpp ../src/storage/database.hpp
    ../src/storage/settings.cpp ../src/storage/settings.hpp
    ../src/security/contentsanitizer.cpp
    ../src/security/riskclassifier.cpp
    ../src/security/secretredactor.cpp
    ../src/security/permissionpolicy.cpp
)

target_include_directories(tst_secretbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(tst_secretbox PRIVATE
    Qt6::Core Qt6::Widgets Qt6::WebEngineWidgets Qt6::Network Qt6::Sql Qt6::WebChannel Qt6::Test
)

add_test(NAME tst_secretbox COMMAND tst_secretbox)
set_tests_properties(tst_secretbox PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 90
)
