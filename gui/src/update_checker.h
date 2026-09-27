// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Compares this build with the latest published GitHub release.
 */
#ifndef ATF150X_PROGRAMMER_GUI_SRC_UPDATE_CHECKER_H_
#define ATF150X_PROGRAMMER_GUI_SRC_UPDATE_CHECKER_H_

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

namespace atf {
namespace gui {

/**
 * @brief Queries the GitHub releases API for a newer version.
 *
 * Lives on the GUI thread. Only reads public release metadata; nothing is
 * downloaded or installed. Draft and pre-release versions are ignored by the
 * "latest" endpoint.
 */
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Creates an idle checker.
     *
     * @param[in] parent Optional Qt parent that takes ownership.
     */
    explicit UpdateChecker(QObject* parent = nullptr);

    /**
     * @brief Starts one asynchronous check.
     *
     * Ignored while a check is already running.
     *
     * @param[in] interactive Passed back unchanged, so callers can decide
     * whether to report "up to date" and errors.
     */
    void Check(bool interactive);

    /**
     * @brief Returns the page listing all releases.
     *
     * @return Browser URL for the project's releases.
     */
    static QUrl ReleasesPage();

signals:
    /**
     * @brief A newer release exists.
     *
     * @param[in] version Release version without the "v" prefix.
     * @param[in] url Browser URL of the release.
     * @param[in] interactive Value given to Check().
     */
    void UpdateAvailable(const QString& version, const QUrl& url,
                         bool interactive);

    /**
     * @brief The running version is the latest release or newer.
     *
     * @param[in] latest Latest release version.
     * @param[in] interactive Value given to Check().
     */
    void UpToDate(const QString& latest, bool interactive);

    /**
     * @brief The check could not be completed.
     *
     * @param[in] error Diagnostic.
     * @param[in] interactive Value given to Check().
     */
    void CheckFailed(const QString& error, bool interactive);

private:
    QNetworkAccessManager network_;
    bool running_ = false;
};

}  // namespace gui
}  // namespace atf

#endif  // ATF150X_PROGRAMMER_GUI_SRC_UPDATE_CHECKER_H_
