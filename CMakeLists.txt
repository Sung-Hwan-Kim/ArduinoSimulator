cmake_minimum_required(VERSION 3.21)
# macOS: 최소 지원 버전 (Qt 6.7 은 macOS 11 이상) - project() 보다 먼저 지정해야 적용됨, 다른 OS 에서는 무시
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "최소 macOS 버전")
# 버전 변경은 여기서만 (exe 속성, 창 제목, 정보 창, INI 에 반영)
project(ArduinoSimulator VERSION 1.0.0 LANGUAGES CXX)

# Windows: Qt 6.7.0 (MSVC 2019 64bit) 기본 경로 - 다른 위치라면 -DCMAKE_PREFIX_PATH=... 로 지정
# Linux  : 시스템 Qt6 (예: Ubuntu qt6-base-dev) 를 자동으로 찾음
# macOS  : -DCMAKE_PREFIX_PATH=~/Qt/6.7.0/macos 처럼 Qt 경로 지정 → ArduinoSimulator.app 생성
if(WIN32 AND NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/Qt/Qt6.7.0/6.7.0/msvc2019_64")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Threads REQUIRED)

# 버전 정보: project(VERSION) 한 곳에서 관리 → Version.h (코드, .rc 에서 사용)
string(TIMESTAMP APP_BUILD_DATE "%Y-%m-%d")
configure_file(${CMAKE_SOURCE_DIR}/src/Version.h.in ${CMAKE_BINARY_DIR}/generated/Version.h @ONLY)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h)

set(APP_RESOURCES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
if(WIN32)
    list(APPEND APP_RESOURCES ${CMAKE_SOURCE_DIR}/resources/app.rc)   # 실행 파일 아이콘 + 버전 정보
elseif(APPLE)
    set(APP_ICNS ${CMAKE_SOURCE_DIR}/resources/app.icns)               # .app 아이콘 (Finder, Dock)
    set_source_files_properties(${APP_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND APP_RESOURCES ${APP_ICNS})
endif()

qt_add_executable(ArduinoSimulator WIN32 MACOSX_BUNDLE ${APP_SOURCES} ${APP_RESOURCES})

if(APPLE)
    set_target_properties(ArduinoSimulator PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.sung-hwan-kim.arduinosimulator"
        MACOSX_BUNDLE_BUNDLE_NAME "아두이노 시뮬레이터"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 김성환 (Sung-Hwan-Kim). MIT License."
        MACOSX_BUNDLE_ICON_FILE app.icns)
endif()

target_include_directories(ArduinoSimulator PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/interpreter)

target_link_libraries(ArduinoSimulator PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Threads::Threads)

if(MSVC)
    # 소스는 UTF-8 (한글 문자열 포함)
    target_compile_options(ArduinoSimulator PRIVATE /utf-8 /W3 /permissive- /MP)
    target_compile_definitions(ArduinoSimulator PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_link_libraries(ArduinoSimulator PRIVATE winmm)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ArduinoSimulator)
    set_target_properties(ArduinoSimulator PROPERTIES
        VS_DEBUGGER_ENVIRONMENT "PATH=${CMAKE_PREFIX_PATH}/bin;%PATH%")
else()
    target_compile_options(ArduinoSimulator PRIVATE -Wall -Wno-unused-parameter -Wno-sign-compare)
endif()

if(WIN32)
    # 빌드 후 Qt DLL 배포 (windeployqt)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ArduinoSimulator POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                    "$<TARGET_FILE:ArduinoSimulator>"
            COMMENT "Qt 런타임 배포 (windeployqt)")
    endif()
elseif(UNIX AND NOT APPLE)
    # Linux: make install / cmake --install 시 실행 파일 + 메뉴 항목 + 아이콘
    include(GNUInstallDirs)
    install(TARGETS ArduinoSimulator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${CMAKE_SOURCE_DIR}/resources/arduinosimulator.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/app.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME arduinosimulator.png)
endif()
