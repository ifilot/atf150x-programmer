// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief GitHub release lookup and semantic version comparison.
 */
#include "gui/src/update_checker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVersionNumber>

#include "firmware/atf1502_programmer/version.h"

namespace atf {
namespace gui {
namespace {

constexpr char kLatestReleaseApi[] =
    "https://api.github.com/repos/ifilot/atf150x-programmer/releases/latest";
constexpr char kReleasesPage[] =
    "https://github.com/ifilot/atf150x-programmer/releases";

}  // namespace

/**
 * @brief Owns its network manager; no request is made until Check().
 */
UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {}

/**
 * @brief Points to the human-readable releases list.
 */
QUrl UpdateChecker::ReleasesPage() {
    return QUrl(QString::fromLatin1(kReleasesPage));
}

/**
 * @brief Requests the latest release and compares its tag with kVersion.
 */
void UpdateChecker::Check(bool interactive) {
    if (running_) {
        return;
    }
    running_ = true;
    QNetworkRequest request(QUrl(QString::fromLatin1(kLatestReleaseApi)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader(
        "User-Agent", QByteArray("atf150x-programmer/") + QByteArray(kVersion));
    request.setTransferTimeout(10000);
    QNetworkReply* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive] {
        reply->deleteLater();
        running_ = false;
        int http_status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http_status == 404) {
            emit CheckFailed(tr("No release has been published yet."),
                             interactive);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit CheckFailed(reply->errorString(), interactive);
            return;
        }
        QJsonObject release =
            QJsonDocument::fromJson(reply->readAll()).object();
        QString tag = release.value(QStringLiteral("tag_name")).toString();
        QString version = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
        QVersionNumber latest = QVersionNumber::fromString(version);
        if (latest.isNull()) {
            emit CheckFailed(tr("Unrecognized release tag \"%1\".").arg(tag),
                             interactive);
            return;
        }
        QVersionNumber current =
            QVersionNumber::fromString(QString::fromLatin1(kVersion));
        if (latest > current) {
            QUrl url(release.value(QStringLiteral("html_url")).toString());
            emit UpdateAvailable(latest.toString(),
                                 url.isValid() ? url : ReleasesPage(),
                                 interactive);
        } else {
            emit UpToDate(latest.toString(), interactive);
        }
    });
}

}  // namespace gui
}  // namespace atf
