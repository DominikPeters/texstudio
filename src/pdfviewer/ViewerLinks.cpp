#include "ViewerLinks.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QMessageBox>

std::optional<QUrl> ViewerLinks::externalUrl(const QString &uri)
{
    if (uri.isEmpty()) return std::nullopt;
    for (const QChar character : uri) {
        const auto code = character.unicode();
        if (code < 0x20 || code == 0x7f) return std::nullopt;
    }
    const QUrl url(uri, QUrl::StrictMode);
    if (!url.isValid() || url.isRelative()) return std::nullopt;
    const QString scheme = url.scheme().toLower();
    if ((scheme == "http" || scheme == "https") && !url.host().isEmpty()) return url;
    if (scheme == "mailto" && !url.path().isEmpty()) return url;
    return std::nullopt;
}

bool ViewerLinks::openUrl(const QUrl &url, QWidget *parent)
{
    if (QDesktopServices::openUrl(url)) return true;
    QMessageBox::warning(parent, QCoreApplication::translate("PDFWidget", "Error"),
        QCoreApplication::translate("PDFWidget", "Could not open link:") + "\n" + url.toString());
    return false;
}
