#include "core/ConfigManager.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("SerialMate"));
    QApplication::setApplicationName(QStringLiteral("SerialMate"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // 主题必须在 MainWindow 建出来之前落地。晚一步的话控件是按旧 QStyle
    // 创建的，开关按钮、菜单、下拉框会留着旧 style 的观感，非得等用户
    // 在「视图 → 主题」里切一次才正常。
    //
    // 这里刻意不走 loadChecked：配置读坏了也照样按 Theme::System 起，
    // 错误提示由 MainWindow 那条路径统一报。早一步报错反而会多弹一次窗。
    applyTheme(ConfigManager::load().theme);

    MainWindow window;
    window.show();
    return app.exec();
}
