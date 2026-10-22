#pragma once

#include <QString>
#include <QUrl>
#include <optional>

class QWidget;

namespace ViewerLinks {
// Live-document URI actions use the same schemes supported by the extension.
std::optional<QUrl> externalUrl(const QString &uri);

// The saved-PDF viewer also calls this with its resolved local-file URLs.
bool openUrl(const QUrl &url, QWidget *parent);
}
