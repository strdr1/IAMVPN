cmake_minimum_required(VERSION 3.21)

# ── Название проекта и версия ───────────────────────────────────────────
# Версия одна на всё — та, что в main.cpp (setApplicationVersion); отсюда
# она попадает в Info.plist macOS-бандла.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" _ver_line
     REGEX "setApplicationVersion\\(QStringLiteral\\(\"[0-9.]+\"\\)\\)")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" IAMVPN_VERSION "${_ver_line}")
project(IAMVPN VERSION ${IAMVPN_VERSION} LANGUAGES CXX)

# Используем C++17 (современный, но не требует свежайшего компилятора)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt автоматически генерирует часть кода (moc/rcc) — включаем это
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Ищем Qt6 (нужны модули Quick для QML-интерфейса) ────────────────────
# Если Qt6 не найдётся, CMake выдаст понятную ошибку — см. README.
find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Network Concurrent QuickControls2 Widgets)

qt_standard_project_setup(REQUIRES 6.5)

# ── Собираем исполняемый файл ───────────────────────────────────────────
qt_add_executable(IAMVPN
    src/main.cpp
    src/ZapretController.h
    src/ZapretController.cpp
    src/NetworkScanner.h
    src/NetworkScanner.cpp
    src/VlessController.h
    src/VlessController.cpp
    src/Store.h
    src/Store.cpp
    src/StatsTracker.h
    src/StatsTracker.cpp
    src/TgController.h
    src/TgController.cpp
    src/DiagController.h
    src/DiagController.cpp
    src/LogController.h
    src/LogController.cpp
    src/AdminCheck.h
    src/CursorHelper.h
    src/TrayMenuHelper.h
    src/ProxyEnv.h
    src/ProxyEnv.cpp
    src/UpdateChecker.h
    src/UpdateChecker.cpp
    src/AppPaths.h
)

# Иконка .exe (Windows-ресурс).
if(WIN32)
    target_sources(IAMVPN PRIVATE app.rc)
endif()

# ── Подключаем QML-модуль (наш интерфейс) ───────────────────────────────
qt_add_qml_module(IAMVPN
    URI IAMVPN
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/StarField.qml
        qml/GlowButton.qml
        qml/GlassPanel.qml
    RESOURCES
        assets/IamvpnLogoLight.png
        assets/tray.png
        assets/appicon.png
        assets/fonts/Manrope-Regular.ttf
)

# ── На Windows: не показывать чёрное консольное окно ─────────────────────
set_target_properties(IAMVPN PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "IAMVPN"
)

# ── Запрос прав администратора при запуске ──────────────────────────────
# Просим линкер встроить UAC-уровень requireAdministrator напрямую (без
# отдельного файла-манифеста, чтобы не конфликтовать с манифестом Qt).
# UAC спросит один раз при старте; winws и sing-box унаследуют права.
if(MSVC)
    target_link_options(IAMVPN PRIVATE
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

# ── Линкуем нужные библиотеки Qt ────────────────────────────────────────
target_link_libraries(IAMVPN PRIVATE
    Qt6::Quick
    Qt6::Network
    Qt6::Concurrent
    Qt6::QuickControls2
    Qt6::Widgets
)

# ── Копируем встроенный движок (winws + WinDivert + lists + профили) ────
# рядом с собранным .exe, в подпапку engine/. Приложение запускает его оттуда.
if(NOT APPLE)
    add_custom_command(TARGET IAMVPN POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/engine"
            "$<TARGET_FILE_DIR:IAMVPN>/engine"
        COMMENT "Copying bundled zapret engine next to the executable"
    )
endif()

# ── macOS: бандл IAMVPN.app ─────────────────────────────────────────────
# Движок для Mac (sing-box universal + собранный TG-прокси) кладёт CI в
# engine-mac/ — он уходит в Contents/Resources/engine. Windows-часть
# (winws, python, .bat) в бандл не попадает. Root-помощник и иконка —
# в Contents/Resources.
if(APPLE)
    set(MAC_RES
        "${CMAKE_SOURCE_DIR}/installer/macos/iamvpn.icns"
        "${CMAKE_SOURCE_DIR}/installer/macos/iamvpn-helper.sh")
    target_sources(IAMVPN PRIVATE ${MAC_RES})
    set_source_files_properties(${MAC_RES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(IAMVPN PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "IAMVPN"
        MACOSX_BUNDLE_GUI_IDENTIFIER "pro.iamvpn.desktop"
        MACOSX_BUNDLE_ICON_FILE "iamvpn.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${IAMVPN_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${IAMVPN_VERSION}"
    )
    if(EXISTS "${CMAKE_SOURCE_DIR}/engine-mac")
        add_custom_command(TARGET IAMVPN POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/engine-mac"
                "$<TARGET_BUNDLE_CONTENT_DIR:IAMVPN>/Resources/engine"
            COMMENT "Copying macOS engine into the app bundle"
        )
    endif()
endif()
