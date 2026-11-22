#pragma once

#include <QString>
#include <QStringList>

/**
 * 客户端运行日志：qInstallMessageHandler 落盘到 AppData/logs 下的滚动文件
 * （单个约 1MB，最多保留 3 个），敏感字段（密码、令牌）写盘前脱敏。
 */
namespace AppLogger {

/** 安装消息处理器（main.cpp 早期调用一次）。 */
void install();

/** 当前日志目录。 */
QString logDir();

/** 收集全部日志文件路径（新→旧）。 */
QStringList logFiles();

/** 对一段文本做敏感信息脱敏。 */
QString sanitize(const QString &text);

} // namespace AppLogger
