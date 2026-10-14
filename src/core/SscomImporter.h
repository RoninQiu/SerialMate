#pragma once

#include "core/AppConfigTypes.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// 一次 SSCOM 配置导入的结果。UI 层据此告诉用户"导了什么、丢了什么、为什么丢"——
// 迁移工具最忌讳的是静默丢数据，用户几个月后才发现配置没迁过来。
struct SscomImportReport
{
    bool        ok = false;
    QString     error;              // 解析失败的原因
    int         itemsImported = 0;  // 实际导入的多字符串条数
    int         itemsDropped  = 0;  // 超出 99 条上限被丢弃的条数
    QStringList notes;              // 逐条说明无法迁移或做了降级处理的项目
};

// 把 SSCOM 5.13.1 的 sscom51.ini 读成 SerialMate 的配置。
//
// 为什么不复用 ConfigManager 直接读那个文件：三层全不兼容。
//   ① 存储位置 —— SSCOM 是 exe 同目录的绿色单文件 ini，SerialMate 现在也是，
//      这一层碰巧对上了；
//   ② 键名结构 —— SSCOM 是扁平数值键（N1080=,COM1），本项目是分组键（serial/port）；
//   ③ 字符编码 —— SSCOM 的 ini 是 GBK，本项目的 ini 是 UTF-8。
// 直接读的结果是满屏乱码，且 SSCOM 自己在文件头就写着「请最好不要用外部编辑器
// 改动本文件」「如果文件被修改后程序不能打开，请删除该文件」。所以只能由程序解析。
class SscomImporter
{
public:
    // 把 SSCOM 的 GBK 字节流解成文本。
    //
    // 刻意走 Win32 的 MultiByteToWideChar(CP936) 而不是 QStringDecoder::System：
    // 后者跟随系统 ACP，在中文 Windows 上恰好能解，换到英文/德文系统就是乱码，
    // 而用 SSCOM 配置的人里一定有在非中文系统上装的。实测本机 ACP=936 时
    // QStringDecoder(System) 也能解，但那是巧合，不能依赖。
    static QString decodeSscomIni(const QByteArray& gbkRaw);

    // 解析并就地合并到 target。
    //
    // 只覆盖 SSCOM ini 里**确实存在**的项；文件里没有的键保持 target 原值，
    // 不做无谓重置。编码是唯一例外，理由见实现处。
    static SscomImportReport import(const QByteArray& gbkRaw, AppConfig& target);
};