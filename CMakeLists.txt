cmake_minimum_required(VERSION 3.16)
project(QtAuthNet VERSION 1.0.0 LANGUAGES CXX)

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Qt 要求
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
find_package(Qt5 REQUIRED COMPONENTS Core Network Gui)

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 库源文件
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
set(LIB_SOURCES
    src/qtauthnet.cpp
    src/qtauthnet_global.h
    src/HttpResponse.cpp
    src/ClientQml.cpp
    src/CasSessionQml.cpp
    src/BrowserCasSession.cpp
)
set(LIB_HEADERS
    include/QtAuthNet/HttpResponse.h
    include/QtAuthNet/ClientQml.h
    include/QtAuthNet/CasSessionQml.h
    include/QtAuthNet/BrowserCasSession.h
)

# 原有 C++ 类（只取 .cpp，.h 已移至 include/QtAuthNet/）
set(ORIGINAL_SOURCES)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/qtauthnet_client.cpp")
    list(APPEND ORIGINAL_SOURCES
        src/qtauthnet_client.cpp
        src/qtauthnet_session.cpp
    )
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# QtAuthNet 库
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
add_library(QtAuthNet SHARED
    ${LIB_SOURCES}
    ${LIB_HEADERS}
    ${ORIGINAL_SOURCES}
)

target_include_directories(QtAuthNet
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(QtAuthNet PUBLIC Qt5::Core Qt5::Network Qt5::Gui)
target_compile_definitions(QtAuthNet PRIVATE QTAUTHNET_LIBRARY)

set_target_properties(QtAuthNet PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    AUTOMOC ON
)

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# QML 插件（可选）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
option(QTAUTHNET_BUILD_QML_PLUGIN "Build QML module plugin" ON)

if(QTAUTHNET_BUILD_QML_PLUGIN)
    find_package(Qt5 REQUIRED COMPONENTS Qml)

    # 复制 qmldir 到 build 目录（moc 需要）
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/qmldir
        ${CMAKE_BINARY_DIR}/qmldir
        COPYONLY
    )

    add_library(QtAuthNetQmlPlugin MODULE
        src/plugin/QtAuthNetPlugin.cpp
        ${CMAKE_BINARY_DIR}/qmldir
    )

    target_link_libraries(QtAuthNetQmlPlugin PRIVATE QtAuthNet Qt5::Qml)
    target_compile_definitions(QtAuthNetQmlPlugin PRIVATE
        QT_STATICPLUGIN=1
        QML_TEST_SPEC_BUILD
    )
    set_target_properties(QtAuthNetQmlPlugin PROPERTIES AUTOMOC ON)

    install(TARGETS QtAuthNetQmlPlugin
        LIBRARY DESTINATION ${CMAKE_INSTALL_QMLDIR}/QtAuthNet
    )
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 安装
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
install(TARGETS QtAuthNet
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${LIB_HEADERS}
    DESTINATION include/QtAuthNet
)

if(QTAUTHNET_BUILD_QML_PLUGIN)
    install(FILES ${CMAKE_BINARY_DIR}/qmldir
        DESTINATION ${CMAKE_INSTALL_QMLDIR}/QtAuthNet
    )
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 测试
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
option(QTAUTHNET_BUILD_TESTS "Build tests" OFF)

if(QTAUTHNET_BUILD_TESTS)
    enable_testing()
    find_package(Qt5 REQUIRED COMPONENTS Test)

    # 只链接 libQtAuthNet.so，所有 moc 文件已在库中
    add_executable(tst_http tests/tst_http.cpp)
    target_include_directories(tst_http PRIVATE
        ${CMAKE_SOURCE_DIR}/tests
    )
    target_link_libraries(tst_http PRIVATE
        QtAuthNet Qt5::Test
    )
    set_target_properties(tst_http PROPERTIES AUTOMOC ON)
    add_test(NAME tst_http COMMAND tst_http)
endif()
