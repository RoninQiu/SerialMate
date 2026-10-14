#pragma once

#include "core/TextCodec.h"
#include "models.h"

#include <QByteArray>
#include <QList>
#include <QString>

// 固定多字符串行数。
//
// 原为需求 FR-04-1 规定的 32 条，现改为 99 条以对齐 SSCOM 5.13.1 ——
// sscom51.ini 实有 N1..N99 共 99 条多字符串（N1xx 是配套的注释行）。
// 32 会让从 SSCOM 迁移的用户直接丢掉三分之二的内容。
constexpr int kSendItemCount = 99;

// 接收区缓冲上限，单位是 UTF-16 码元数（对 ASCII/二进制为主的串口数据
// 基本等同于字节数）。SSCOM 5.13.1 用字节制：sscom51.ini 的 N1067 默认
// 1000000，预设 200K/500K/1M/2M/5M/10M/20M（ini 注释里逐项列明）。
// 超出上限时从最早的数据开始丢弃，保证内存有界。
//
// 必须定义在 AppConfig **之前**：下面 recvBufferLimit 拿它做默认成员
// 初值，顺序反了编译期就报 "not declared in this scope"。
constexpr qsizetype kDefaultReceiveLimit = 1000000;

// 窗口布局结构版本号。
//
// QMainWindow::saveState() 存下来的 blob 是**贴着当时那套控件树**的：
// 它按顺序记录每个具名子控件的可见性与分隔条尺寸。布局一改，旧 blob 就对不上了，
// 而且不会报错 —— restoreState() 会照着旧结构把新控件设成隐藏，返回值仍是 true。
// 实测踩过：两栏布局的旧 blob 会让三栏布局里的多字符串面板整个消失。
//
// 所以凡��调整分隔条结构或增删具名控件，都要 +1。版本对不上就只用默认布局，
// 宁可让用户的位置重设一次，也不能给他一个打不开的面板。
constexpr int kLayoutVersion = 2;

// 全部需要跨会话保留的状态。
//
// 刻意从 ConfigManager.h 拆出来：UI 层引用这份纯数据结构时不必连带引入
// QSettings。MultiSendTable.cpp 只需要 kSendItemCount，若走 ConfigManager.h
// 会把整个 QSettings 依赖拖进 UI 目标。
struct AppConfig
{
    SerialConfig    serial;
    QList<SendItem> sendItems;            // 恒为 kSendItemCount 条
    QString         sendText;
    bool            sendAppendCrlf  = false;
    bool            sendHexMode     = false;
    int             sendIntervalMs  = 1000;
    TextEncoding   sendEncoding    = TextEncoding::Utf8;
    bool            loopEnabled     = false;
    int             loopIntervalMs  = 100;

    bool            recvHex         = false;
    bool            recvTimestamp   = false;
    bool            recvWrap        = true;
    bool            recvShowSent    = false;
    qsizetype       recvBufferLimit = kDefaultReceiveLimit;
    TextEncoding   recvEncoding    = TextEncoding::Utf8;
    QString         recvFont;               // QFont::toString()；空串表示用默认字体

    QByteArray      geometry;               // saveGeometry() 结果
    QByteArray      state;                  // saveState() 结果，仅当 layoutVersion 匹配时才可用
    int             layoutVersion   = 0;    // 读到 0 表示这份配置来自无版本号的老版本
};