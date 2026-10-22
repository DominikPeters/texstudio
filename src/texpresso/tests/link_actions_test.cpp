#include <QtTest>
#include "ViewerLinks.h"
#include <QDesktopServices>

class LinkHandler : public QObject
{
    Q_OBJECT
public:
    QList<QUrl> opened;
public slots:
    void handle(const QUrl &url) { opened.append(url); }
};

class ViewerLinksTest : public QObject
{
    Q_OBJECT
private slots:
    void externalSchemes_data();
    void externalSchemes();
    void controlCharacters();
    void desktopDispatchPreservesNormalViewerUrls();
};

void ViewerLinksTest::externalSchemes_data()
{
    QTest::addColumn<QString>("uri");
    QTest::addColumn<bool>("accepted");
    QTest::newRow("https") << "https://example.com/paper#result" << true;
    QTest::newRow("http") << "http://example.com/?q=a%20b" << true;
    QTest::newRow("uppercase-scheme") << "HTTPS://example.com/" << true;
    QTest::newRow("mailto") << "mailto:author@example.com?subject=Paper" << true;
    QTest::newRow("unicode-host") << QString::fromUtf8("https://éxample.org/") << true;
    QTest::newRow("empty") << "" << false;
    QTest::newRow("empty-mailto") << "mailto:" << false;
    QTest::newRow("hostless") << "https:/paper" << false;
    QTest::newRow("malformed-host") << "https://[invalid/" << false;
    QTest::newRow("malformed-escape") << "https://example.com/%xx" << false;
    QTest::newRow("relative") << "paper.pdf" << false;
    QTest::newRow("fragment") << "#result" << false;
    QTest::newRow("protocol-relative") << "//example.com/paper" << false;
    QTest::newRow("local-file") << "file:///tmp/paper.pdf" << false;
    QTest::newRow("javascript") << "javascript:alert(1)" << false;
    QTest::newRow("command") << "command:evil" << false;
    QTest::newRow("data") << "data:text/html,content" << false;
}

void ViewerLinksTest::externalSchemes()
{
    QFETCH(QString, uri);
    QFETCH(bool, accepted);
    const auto parsed = ViewerLinks::externalUrl(uri);
    QCOMPARE(parsed.has_value(), accepted);
    if (parsed) {
        QVERIFY(parsed->isValid());
        QVERIFY(!parsed->isRelative());
    }
}

void ViewerLinksTest::controlCharacters()
{
    // QUrl may otherwise trim or percent-encode controls while parsing.
    for (int code = 0; code <= 0x7f; ++code) {
        if (code >= 0x20 && code != 0x7f) continue;
        const QString uri = "https://example.com/" + QString(QChar(code)) + "paper";
        QVERIFY2(!ViewerLinks::externalUrl(uri), qPrintable(QString::number(code)));
    }
}

void ViewerLinksTest::desktopDispatchPreservesNormalViewerUrls()
{
    LinkHandler handler;
    const QList<QUrl> urls = {
        QUrl("https://example.com/paper#result"),
        QUrl("mailto:author@example.com"),
        QUrl::fromLocalFile("/tmp/a paper.pdf"),
        QUrl("custom-reader:paper")
    };
    for (const auto &url : urls)
        QDesktopServices::setUrlHandler(url.scheme(), &handler, "handle");
    for (const auto &url : urls)
        QVERIFY(ViewerLinks::openUrl(url, nullptr));
    for (const auto &url : urls)
        QDesktopServices::unsetUrlHandler(url.scheme());
    QCOMPARE(handler.opened, urls);
}

QTEST_MAIN(ViewerLinksTest)
#include "link_actions_test.moc"
