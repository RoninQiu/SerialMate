cmake_minimum_required(VERSION 3.21)
project(SerialMate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets SerialPort Test)

if(MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

set(SSCOM_CORE_SOURCES
    src/models.h
    src/core/AppConfigTypes.h
    src/core/TextCodec.h
    src/core/TextCodec.cpp
    src/core/DataFormatter.h
    src/core/DataFormatter.cpp
    src/core/SendQueueManager.h
    src/core/SendQueueManager.cpp
    src/core/ConfigManager.h
    src/core/ConfigManager.cpp
    src/core/SscomImporter.h
    src/core/SscomImporter.cpp
    src/core/LogWriter.h
    src/core/LogWriter.cpp
    src/core/SerialPortManager.h
    src/core/SerialPortManager.cpp
)

set(SSCOM_UI_SOURCES
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/SerialConfigPanel.h
    src/ui/SerialConfigPanel.cpp
    src/ui/ReceiveView.h
    src/ui/ReceiveView.cpp
    src/ui/SendPanel.h
    src/ui/SendPanel.cpp
    src/ui/MultiSendTable.h
    src/ui/MultiSendTable.cpp
    src/ui/WrapLayout.h
    src/ui/WrapLayout.cpp
)

qt_add_executable(SerialMate
    src/main.cpp
    ${SSCOM_CORE_SOURCES}
    ${SSCOM_UI_SOURCES}
)
target_include_directories(SerialMate PRIVATE src)
target_link_libraries(SerialMate PRIVATE Qt6::Widgets Qt6::SerialPort)

# PE 子系统必须是 GUI，否则双击会多弹一个黑框。
#
# 实测过：objdump -p SerialMate.exe 里 Subsystem = 00000003 (Windows CUI)，
# CUI 就是 Console User Interface，Windows 会给它分配控制台，黑色窗口就跟在
# 主窗口后面。WIN32_EXECUTABLE 会加上 -mwindows，子系统变成 00000002。
#
# 这是 PE 头里的一个字段，跟打包方式、跟 dist 里带不带 DLL 完全无关 ——
# 同一个 exe、同一批 DLL，只是双击时不再多出那个终端。
#
# 前提是程序不往 stdout/stderr 写东西：GUI 子系统没有可附加的控制台，
# printf 之类会静默丢掉。已核对 src/main.cpp 不碰标准输出，
# 所有报错都走 QMessageBox，所以丢掉也不丢信息。
#
# 只给主程序加，四个测试目标必须留在 CUI —— ctest 靠它们的标准输出看断言结果。
set_target_properties(SerialMate PROPERTIES WIN32_EXECUTABLE TRUE)

enable_testing()

qt_add_executable(tst_dataformatter
    tests/tst_dataformatter.cpp
    src/core/DataFormatter.h
    src/core/DataFormatter.cpp
    src/core/TextCodec.h
    src/core/TextCodec.cpp
)
target_include_directories(tst_dataformatter PRIVATE src)
target_link_libraries(tst_dataformatter PRIVATE Qt6::Test)
add_test(NAME dataformatter COMMAND tst_dataformatter)

qt_add_executable(tst_textcodec
    tests/tst_textcodec.cpp
    src/core/TextCodec.h
    src/core/TextCodec.cpp
)
target_include_directories(tst_textcodec PRIVATE src)
target_link_libraries(tst_textcodec PRIVATE Qt6::Test)
add_test(NAME textcodec COMMAND tst_textcodec)

qt_add_executable(tst_sendqueuemanager
    tests/tst_sendqueuemanager.cpp
    src/core/DataFormatter.h
    src/core/DataFormatter.cpp
    src/core/SendQueueManager.h
    src/core/SendQueueManager.cpp
    # DataFormatter/SendQueueManager 都调用 TextCodec，缺它会 undefined reference
    src/core/TextCodec.h
    src/core/TextCodec.cpp
)
target_include_directories(tst_sendqueuemanager PRIVATE src)
# models.h 会 include <QSerialPort>，所以这个测试目标也需要 SerialPort 的头文件路径。
# 只链 Qt6::Test 会报 "QSerialPort: No such file or directory"。
target_link_libraries(tst_sendqueuemanager PRIVATE Qt6::Test Qt6::SerialPort)
add_test(NAME sendqueuemanager COMMAND tst_sendqueuemanager)

qt_add_executable(tst_sscomimporter
    tests/tst_sscomimporter.cpp
    src/core/SscomImporter.h
    src/core/SscomImporter.cpp
    # SscomImporter 通过 AppConfigTypes.h 引入 TextCodec.h，缺它会 undefined reference
    src/core/TextCodec.h
    src/core/TextCodec.cpp
)
target_include_directories(tst_sscomimporter PRIVATE src)
# 同上：AppConfigTypes.h -> models.h -> <QSerialPort>；另需 QtGui —— QFont 在 QtGui 里，
# 字体映射（N1088/N1089 -> recv/font）要用它。漏链会报 "No such file or directory: QFont"
target_link_libraries(tst_sscomimporter PRIVATE Qt6::Test Qt6::SerialPort Qt6::Gui)
add_test(NAME sscomimporter COMMAND tst_sscomimporter)
