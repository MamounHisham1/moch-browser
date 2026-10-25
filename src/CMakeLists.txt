qt_add_executable(moch
    main.cpp
    app/applicationcontroller.cpp app/applicationcontroller.hpp
    browser/mochurlscheme.cpp browser/mochurlscheme.hpp
    browser/tabmanager.cpp browser/tabmanager.hpp
    browser/urlresolver.cpp browser/urlresolver.hpp
    browser/webview.cpp browser/webview.hpp
    ui/mainwindow.cpp ui/mainwindow.hpp
    ui/tabbar.cpp ui/tabbar.hpp
    ui/toolbar.cpp ui/toolbar.hpp
    storage/settings.cpp storage/settings.hpp
    resources/moch.qrc
)

target_include_directories(moch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(moch PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebEngineWidgets Qt6::Network Qt6::Sql
)
