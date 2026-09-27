// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Application entry point of the ATF150x Programmer GUI.
 */
#include <QApplication>
#include <QIcon>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>

#include "firmware/atf1502_programmer/version.h"
#include "gui/src/main_window.h"

/**
 * @brief Applies the Windows Vista style and opens an optional JEDEC file.
 *
 * @param[in] argc Process argument count.
 * @param[in] argv Process arguments; the first non-option argument is opened.
 * @return Qt event loop exit code.
 */
int main(int argc, char** argv) {
    QApplication::setOrganizationName(QStringLiteral("atf150x-programmer"));
    QApplication::setApplicationName(QStringLiteral("ATF150x Programmer"));
    QApplication::setApplicationVersion(QString::fromLatin1(atf::kVersion));
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/atf150x-programmer.png")));
#ifdef Q_OS_WIN
    // Qt 6 defaults to the Windows 11 style; keep the classic native look.
    if (QStyle* style = QStyleFactory::create(QStringLiteral("windowsvista"))) {
        app.setStyle(style);
    }
#endif

    atf::gui::MainWindow window;
    window.show();
    QStringList arguments = app.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (!arguments[i].startsWith(QLatin1Char('-'))) {
            window.OpenFile(arguments[i]);
            break;
        }
    }
    return app.exec();
}
