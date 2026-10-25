qt_add_executable(moch
    main.cpp
    ai/provider.cpp ai/provider.hpp
    ai/openaicompatprovider.cpp ai/openaicompatprovider.hpp
    ai/modelcatalog.cpp ai/modelcatalog.hpp
    ai/providerregistry.cpp ai/providerregistry.hpp
    app/applicationcontroller.cpp app/applicationcontroller.hpp
    browser/bookmarkstore.cpp browser/bookmarkstore.hpp
    browser/downloadmanager.cpp browser/downloadmanager.hpp
    browser/historystore.cpp browser/historystore.hpp
    browser/mochurlscheme.cpp browser/mochurlscheme.hpp
    browser/newtabbridge.cpp browser/newtabbridge.hpp
    browser/openrouterauth.cpp browser/openrouterauth.hpp
    browser/setupbridge.cpp browser/setupbridge.hpp
    browser/tabmanager.cpp browser/tabmanager.hpp
    browser/urlresolver.cpp browser/urlresolver.hpp
    browser/webview.cpp browser/webview.hpp
    ui/aisidebar.cpp ui/aisidebar.hpp
    ui/downloadspanel.cpp ui/downloadspanel.hpp
    ui/mainwindow.cpp ui/mainwindow.hpp
    ui/settingsdialog.cpp ui/settingsdialog.hpp
    ui/tabbar.cpp ui/tabbar.hpp
    ui/toolbar.cpp ui/toolbar.hpp
    tools/tool.cpp tools/tool.hpp
    tools/toolregistry.cpp tools/toolregistry.hpp
    tools/pagetools.cpp tools/pagetools.hpp
    storage/database.cpp storage/database.hpp
    storage/settings.cpp storage/settings.hpp
    security/contentsanitizer.cpp security/contentsanitizer.hpp
    security/riskclassifier.cpp security/riskclassifier.hpp
    security/secretredactor.cpp security/secretredactor.hpp
    security/permissionpolicy.cpp security/permissionpolicy.hpp
    agent/agentservice.cpp agent/agentservice.hpp
    agent/approvalgate.cpp agent/approvalgate.hpp
    resources/moch.qrc
)

target_include_directories(moch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(moch PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebEngineWidgets Qt6::Network Qt6::Sql Qt6::WebChannel
)
