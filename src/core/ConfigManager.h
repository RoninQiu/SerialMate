#pragma once

#include "core/AppConfigTypes.h"

#include <QString>

// 配置的读取与落盘。
//
// 刻意做成"绿色软件"：配置写在 exe 同目录的 SerialMate.ini，不碰注册表。
// 判据是 SSCOM 5.13.1 的形态 —— exe 和 sscom51.ini 摆一起就能用，整个目录
// 拷到 U 盘或别的电脑上照常工作，删掉 ini 就恢复默认值。
//
// 早期版本用 QSettings(org, app)，在 Windows 上默认落到
// HKCU\Software\SerialMate\SerialMate。那是"装了软件"的形态，不是绿色软件：
// 配置藏在注册表里，用户看不见、删不掉、换机带不走，卸载后还留残留。
class ConfigManager
{
public:
    // 配置文件绝对路径，即 <exe 所在目录>/SerialMate.ini。
    static QString configFilePath();

    // 启动时探测配置到底写不写得进去。写不进去时越早发现越好 ——
    // 等用户配了半小时参数、关程序时才发现没存上，是最糟的体验。
    // 典型触发场景：程序被放进了 Program Files 且当前用户无写权限。
    static bool checkWritable(QString* error);

    static AppConfig load();
    // 同 load()，但把「文件读坏了」这件事报出来。
    //
    // QSettings 的读取是惰性的，读不出来时每条 value() 都静默返回默认值，
    // 不检查 status() 的话 load() 永远只会返回一份全新的默认配置：用户那份
    // 99 条、发送草稿、窗口布局全部"消失"，而退出时 MainWindow 又会把这份
    // 默认值原样写回 —— 损坏的文件被永久覆盖，且全程没有任何提示。
    static AppConfig loadChecked(QString* error);
    static void save(const AppConfig& cfg);

    // 返回是否落盘成功。失败时（目录只读、磁盘满）error 带上原因。
    static bool saveChecked(const AppConfig& cfg, QString* error);
};