cmake_minimum_required(VERSION 3.21)
# 버전 변경은 여기서만 (exe 속성, 창 제목, 정보 창, INI 에 반영)
project(ArduinoSimulator VERSION 1.0.0 LANGUAGES CXX)

# Qt 6.7.0 (MSVC 2019 64bit) 기본 경로 - 다른 위치라면 -DCMAKE_PREFIX_PATH=... 로 지정
if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/Qt/Qt6.7.0/6.7.0/msvc2019_64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# 버전 정보: project(VERSION) 한 곳에서 관리 → Version.h (코드, .rc 에서 사용)
string(TIMESTAMP APP_BUILD_DATE "%Y-%m-%d")
configure_file(${CMAKE_SOURCE_DIR}/src/Version.h.in ${CMAKE_BINARY_DIR}/generated/Version.h @ONLY)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h)

set(APP_RESOURCES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
if(WIN32)
    list(APPEND APP_RESOURCES ${CMAKE_SOURCE_DIR}/resources/app.rc)   # 실행 파일 아이콘 + 버전 정보
endif()

qt_add_executable(ArduinoSimulator WIN32 ${APP_SOURCES} ${APP_RESOURCES})

target_include_directories(ArduinoSimulator PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/interpreter)

target_link_libraries(ArduinoSimulator PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

if(MSVC)
    # 소스는 UTF-8 (한글 문자열 포함)
    target_compile_options(ArduinoSimulator PRIVATE /utf-8 /W3 /permissive- /MP)
    target_compile_definitions(ArduinoSimulator PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_link_libraries(ArduinoSimulator PRIVATE winmm)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ArduinoSimulator)
    set_target_properties(ArduinoSimulator PROPERTIES
        VS_DEBUGGER_ENVIRONMENT "PATH=${CMAKE_PREFIX_PATH}/bin;%PATH%")
endif()

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
