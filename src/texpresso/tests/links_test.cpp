#include <QtTest>
#include "texpressopreview.h"
#include "texpressoprotocol.h"
#include "texpressomanager.h"
#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <limits>

class LinksTest : public QObject
{
    Q_OBJECT
private slots:
    void annotationValidation();
    void externalLinksAndStaleFrames();
    void internalDestinations();
    void interruptedDestinations();
    void inactiveLinksAndDestinationTimeout();
    void cachedAndBackgroundLinks();
    void emptyPatchUpdatesAnnotations();
    void realHyperref();
};

static QJsonObject annotations(const QStringList &uris, const QString &destination = {})
{
    QJsonArray links;
    for (const auto &uri : uris)
        links.append(QJsonObject{{"uri", uri}, {"bounds", QJsonArray{20, 30, 100, 50}}});
    QJsonArray destinations;
    if (!destination.isEmpty()) destinations.append(QJsonObject{{"name", destination}, {"x", 40}, {"y", 100}});
    return {{"links", links}, {"destinations", destinations}};
}
static QJsonObject frameHeader(int length)
{
    return {{"requestId", 1}, {"revision", 1}, {"sequence", 0}, {"page", 0},
        {"width", 16}, {"height", 24}, {"bounds", QJsonArray{10, 20, 170, 260}},
        {"mime", "image/png"}, {"byteLength", length}};
}
static QJsonArray lastCommand(const QSignalSpy &commands) { return commands.last()[0].toJsonArray(); }
static QByteArray png()
{
    QImage image(16, 24, QImage::Format_RGB32); image.fill(Qt::red);
    QByteArray data; QBuffer buffer(&data); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
    return data;
}
static void deliver(TexpressoPreview &preview, const QJsonArray &request, const QJsonObject &metadata, int raster = 0)
{
    const auto bytes = png(); auto header = frameHeader(bytes.size());
    header["requestId"] = request[1]; header["page"] = request[2];
    header["revision"] = request[4]; header["sequence"] = request[5]; header["annotations"] = metadata;
    if (raster) header["rasterId"] = raster;
    preview.receive(header, bytes);
}
static int linkIndex(const QJsonObject &metadata, const QString &uri)
{
    const auto links = metadata["links"].toArray();
    for (int i = 0; i < links.size(); ++i) if (links[i].toObject()["uri"].toString() == uri) return i;
    return -1;
}

void LinksTest::annotationValidation()
{
    auto header = frameHeader(20); QVERIFY(TexpressoProtocol::validImage(header));
    const auto metadata = annotations({"https://example.com/?x=1&y=2", "#é😀"}, "é😀");
    header["annotations"] = metadata; QVERIFY(TexpressoProtocol::validImage(header));
    const auto rejected = [&](const QJsonValue &value) {
        auto invalid = header; invalid["annotations"] = value;
        return !TexpressoProtocol::validImage(invalid);
    };
    QVERIFY(rejected(QJsonArray{}));
    QVERIFY(rejected(QJsonObject{{"links", QJsonArray{} }}));
    QVERIFY(rejected(QJsonObject{{"links", QJsonArray{}}, {"destinations", "bad"}}));
    for (const QJsonArray &bounds : {QJsonArray{0, 0, 1}, QJsonArray{4, 0, 2, 3},
             QJsonArray{0, 4, 2, 3}, QJsonArray{0, 0, 0, 2}, QJsonArray{0, 0, "1", 2},
             QJsonArray{0, 0, std::numeric_limits<double>::infinity(), 2}}) {
        auto bad = metadata;
        bad["links"] = QJsonArray{QJsonObject{{"uri", "https://example.com"}, {"bounds", bounds}}};
        QVERIFY(rejected(bad));
    }
    for (const auto &uri : {QString(4097, 'x'), QString("https://example.com/\nunsafe")}) {
        QVERIFY(rejected(annotations({uri})));
    }
    auto bad = metadata; bad["links"] = QJsonArray{7}; QVERIFY(rejected(bad));
    bad = metadata; bad["destinations"] = QJsonArray{QJsonObject{{"name", "x"}, {"x", "2"}, {"y", 1}}};
    QVERIFY(rejected(bad));
    bad["destinations"] = QJsonArray{QJsonObject{{"name", QString(4097, 'x')}, {"x", 2}, {"y", 1}}};
    QVERIFY(rejected(bad));
    bad["destinations"] = QJsonArray{QJsonObject{{"name", "bad\ntarget"}, {"x", 2}, {"y", 1}}};
    QVERIFY(rejected(bad));
    bad["destinations"] = QJsonArray{QJsonObject{{"name", "target"}, {"x", 2},
        {"y", std::numeric_limits<double>::quiet_NaN()}}};
    QVERIFY(rejected(bad));
    QJsonArray many;
    for (int i = 0; i < 4096; ++i) many.append(metadata["links"].toArray()[0]);
    bad = metadata; bad["links"] = many; header["annotations"] = bad; QVERIFY(TexpressoProtocol::validImage(header));
    many.append(many[0]); bad["links"] = many; QVERIFY(rejected(bad));
    many = {};
    for (int i = 0; i < 4097; ++i) many.append(metadata["destinations"].toArray()[0]);
    bad = metadata; bad["destinations"] = many; QVERIFY(rejected(bad));
}

void LinksTest::externalLinksAndStaleFrames()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    QSignalSpy external(&preview, &TexpressoPreview::externalLinkRequested);
    preview.setCapabilities({{"links", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 1, true, 0});
    const auto metadata = annotations({"https://example.com/a?x=1&y=2", "http://example.com", "mailto:test@example.com",
        "javascript:alert(1)", "command:evil", "file:///tmp/private", "data:text/html,hello", "https:///missing-host"});
    deliver(preview, lastCommand(commands), metadata); QTRY_VERIFY(preview.valid());
    QCOMPARE(preview.annotationsForPage(0), metadata);
    for (int i = 0; i < 8; ++i) preview.activateLink(0, i, 1, 0);
    QCOMPARE(external.size(), 3);
    QCOMPARE(external[0][0].toUrl(), QUrl("https://example.com/a?x=1&y=2"));
    QCOMPARE(external[2][0].toUrl(), QUrl("mailto:test@example.com"));
    preview.activateLink(0, -1, 1, 0); preview.activateLink(0, 1000, 1, 0);
    preview.activateLink(0, 0, 0, 0); preview.activateLink(0, 0, 1, 1); preview.activateLink(99, 0, 1, 0);
    QCOMPARE(external.size(), 3);
    preview.sourceChanged(1); QVERIFY(preview.annotationsForPage(0).isEmpty());
    preview.activateLink(0, 0, 1, 0); preview.activateLink(0, 0, 1, 1);
    QCOMPARE(external.size(), 3);
}

void LinksTest::internalDestinations()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    QSignalSpy reveals(&preview, &TexpressoPreview::reveal);
    QSignalSpy failures(&preview, &TexpressoPreview::failed);
    preview.setCapabilities({{"links", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 1, false, 0});
    deliver(preview, lastCommand(commands), annotations({"#later", "#missing"})); QTRY_VERIFY(preview.valid());
    preview.activateLink(0, 0, 1, 0);
    QCOMPARE(lastCommand(commands), (QJsonArray{"preview-destination", 1, 0, "later"}));
    const int before = commands.size();
    preview.message({"preview-destination", 0, 0, "later", 2});
    preview.message({"preview-destination", 1, 1, "later", 2});
    preview.message({"preview-destination", 1, 0, "other", 2});
    QCOMPARE(commands.size(), before); QCOMPARE(preview.page(), 0);
    // The engine can compile ahead while resolving a previously unseen target.
    preview.message({"preview-state", 1, 0, 3, true, 0});
    preview.message({"preview-destination", 1, 0, "later", 2});
    QCOMPARE(preview.page(), 2); QCOMPARE(lastCommand(commands)[0].toString(), QString("preview-render"));
    deliver(preview, lastCommand(commands), annotations({}, "later"));
    QTRY_VERIFY(preview.valid()); QTRY_COMPARE(reveals.size(), 1);
    QCOMPARE(reveals[0][0].toPointF(), QPointF(40, 100));
    preview.setPage(0); QVERIFY(preview.valid());
    preview.activateLink(0, 1, 1, 0); preview.message({"preview-destination", 1, 0, "missing", -1});
    QVERIFY(!preview.busy()); QCOMPARE(preview.page(), 0); QCOMPARE(reveals.size(), 1);
    QCOMPARE(failures.size(), 1); QVERIFY(!failures[0][0].toString().isEmpty());
    // A target cached earlier must still reveal its exact anchor.
    preview.activateLink(0, 0, 1, 0); preview.message({"preview-destination", 1, 0, "later", 2});
    QTRY_COMPARE(reveals.size(), 2); QCOMPARE(preview.page(), 2);
}

void LinksTest::interruptedDestinations()
{
    for (int interruption = 0; interruption < 3; ++interruption) {
        TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
        QSignalSpy reveals(&preview, &TexpressoPreview::reveal);
        preview.setCapabilities({{"links", true}}); preview.setActive(true);
        preview.message({"preview-state", 1, 0, 3, true, 0});
        deliver(preview, lastCommand(commands), annotations({"#later"})); QTRY_VERIFY(preview.valid());
        preview.activateLink(0, 0, 1, 0);
        preview.setViewportPages({1, 0});
        QCOMPARE(preview.page(), 0); // A queued layout is not a user navigation.
        if (interruption == 0) preview.setPage(1);
        else if (interruption == 1) preview.sourceChanged(1);
        else preview.forward("main.tex", 4);
        const int count = commands.size();
        preview.message({"preview-destination", 1, 0, "later", 2});
        QCOMPARE(commands.size(), count); QVERIFY(preview.page() != 2); QVERIFY(reveals.isEmpty());
    }
}

void LinksTest::inactiveLinksAndDestinationTimeout()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    QSignalSpy external(&preview, &TexpressoPreview::externalLinkRequested);
    QSignalSpy failures(&preview, &TexpressoPreview::failed);
    preview.setCapabilities({{"links", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 1, true, 0});
    deliver(preview, lastCommand(commands), annotations({"https://example.com", "#target"})); QTRY_VERIFY(preview.valid());
    preview.setActive(false); const int paused = commands.size();
    preview.activateLink(0, 0, 1, 0); preview.activateLink(0, 1, 1, 0);
    QVERIFY(external.isEmpty()); QCOMPARE(commands.size(), paused);
    preview.setActive(true); preview.setCapabilities({}); const int unsupported = commands.size();
    preview.activateLink(0, 0, 1, 0); preview.activateLink(0, 1, 1, 0);
    QVERIFY(external.isEmpty()); QCOMPARE(commands.size(), unsupported);
    preview.setCapabilities({{"links", true}});
    preview.activateLink(0, 1, 1, 0); QVERIFY(preview.busy());
    // Invoke the watchdog's ordinary signal without a thirty-second test sleep.
    QVERIFY(QMetaObject::invokeMethod(&preview.destinationTimer, "timeout", Qt::DirectConnection));
    QVERIFY(!preview.busy()); QCOMPARE(failures.size(), 1); QVERIFY(!failures[0][0].toString().isEmpty());
    preview.activateLink(0, 1, 1, 0); preview.setActive(false); preview.setActive(true);
    const int resumed = commands.size();
    preview.message({"preview-destination", 1, 0, "target", 0});
    QCOMPARE(commands.size(), resumed); QVERIFY(!preview.busy());
}

void LinksTest::cachedAndBackgroundLinks()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    QSignalSpy external(&preview, &TexpressoPreview::externalLinkRequested);
    preview.setCapabilities({{"links", true}, {"backgroundRendering", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 3, true, 0});
    const auto first = annotations({"https://example.com/first"});
    const auto second = annotations({"https://example.com/second"});
    deliver(preview, lastCommand(commands), first); QTRY_VERIFY(preview.valid());
    preview.setViewportPages({0, 1}); QCOMPARE(lastCommand(commands)[0].toString(), QString("preview-background"));
    deliver(preview, lastCommand(commands), second); QTRY_VERIFY(preview.pageValid(1));
    QCOMPARE(preview.page(), 0); QCOMPARE(preview.annotationsForPage(1), second);
    preview.activateLink(1, 0, 1, 0); QCOMPARE(external.size(), 1);
    QCOMPARE(external[0][0].toUrl(), QUrl("https://example.com/second"));
    preview.setPage(1); QVERIFY(preview.valid()); QCOMPARE(preview.annotationsForPage(1), second);
    preview.setPage(0); QVERIFY(preview.valid()); QCOMPARE(preview.annotationsForPage(0), first);
    preview.message({"preview-invalidated", 2, 0});
    QVERIFY(preview.annotationsForPage(0).isEmpty()); QVERIFY(preview.annotationsForPage(1).isEmpty());
    preview.activateLink(1, 0, 1, 0); QCOMPARE(external.size(), 1);
}

void LinksTest::emptyPatchUpdatesAnnotations()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    QSignalSpy external(&preview, &TexpressoPreview::externalLinkRequested);
    preview.setCapabilities({{"links", true}, {"pngPatches", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 1, true, 0});
    deliver(preview, lastCommand(commands), annotations({"https://example.com/old"}), 11); QTRY_VERIFY(preview.valid());
    const auto pixels = preview.image();
    preview.invalidateNativePage(); preview.inverse({40, 100});
    const auto request = lastCommand(commands); QCOMPARE(request[0].toString(), QString("preview-png"));
    auto header = frameHeader(0); header["requestId"] = request[1];
    header["rasterId"] = 12; header["patch"] = QJsonObject{{"base", 11}, {"rect", QJsonArray{0, 0, 0, 0}}};
    const auto next = annotations({"https://example.com/new"}); header["annotations"] = next;
    preview.receive(header, {}); QTRY_COMPARE(preview.annotationsForPage(0), next);
    QCOMPARE(preview.image(), pixels); preview.activateLink(0, 0, 1, 0); QCOMPARE(external.size(), 1);
    QCOMPARE(external[0][0].toUrl(), QUrl("https://example.com/new"));
}

void LinksTest::realHyperref()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for hyperref integration");
    QTemporaryDir project; const auto root = project.filePath("links.tex");
    const QString source = QString::fromUtf8(R"TEX(\documentclass{article}
\usepackage[xetex,hidelinks]{hyperref}
\begin{document}
\hypertarget{first}{First page.}
\href{https://example.com/a?x=1\&y=2}{External web link}.
\hyperlink{later}{Jump to a later page}.
\hyperlink{missing}{An unavailable target}.
\parbox{90pt}{\href{mailto:test@example.com}{This email link wraps across several lines of text.}}
\tableofcontents
Section \ref{sec:later}; citation \cite{foo}.
\newpage
\hypertarget{later}{Later target.}
\section{Later section}\label{sec:later}
\hyperlink{first}{Back to first page}.
\newpage
\begin{thebibliography}{1}
\bibitem{foo} A cited reference.
\end{thebibliography}
\end{document}
)TEX");
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(source.toUtf8()), source.toUtf8().size()); file.close();
    TexpressoManager manager(nullptr, engine); manager.activate(root); auto session = manager.current();
    session->setAutoRerun(false); session->followCursor = false;
    QSignalSpy reveals(&session->preview, &TexpressoPreview::reveal);
    QSignalSpy external(&session->preview, &TexpressoPreview::externalLinkRequested);
    QSignalSpy failures(&session->preview, &TexpressoPreview::failed);
    const auto diagnostics = [&] {
        return QString("page=%1 count=%2 valid=%3 complete=%4 revision=%5 sequence=%6 busy=%7 status=%8\nCompiler:\n%9\nEngine:\n%10")
            .arg(session->preview.page()).arg(session->preview.pageCount()).arg(session->preview.valid())
            .arg(session->preview.complete()).arg(session->preview.revision()).arg(session->preview.sequence())
            .arg(session->preview.busy()).arg(session->status).arg(session->compilerLogOutput().right(5000))
            .arg(session->engineOutput.right(8000));
    };
    const auto wait = [&](const char *stage, const std::function<bool()> &ready) {
        qInfo() << "Hyperref stage:" << stage;
        QElapsedTimer deadline; deadline.start();
        while (!ready() && deadline.elapsed() < 20000) QTest::qWait(15);
        if (!ready()) qWarning().noquote() << stage << diagnostics();
        return ready();
    };
    QVERIFY2(wait("first image", [&] { return session->preview.valid(); }), qPrintable(diagnostics()));
    auto metadata = session->preview.annotationsForPage(0);
    QVERIFY2(linkIndex(metadata, "#later") >= 0, qPrintable(session->engineOutput));
    QVERIFY(linkIndex(metadata, "https://example.com/a?x=1&y=2") >= 0);
    int mailRuns = 0;
    for (const auto &value : metadata["links"].toArray()) {
        const auto link = value.toObject(); const auto bounds = link["bounds"].toArray();
        QVERIFY(bounds[0].toDouble() > 0); QVERIFY(bounds[1].toDouble() > 0);
        QVERIFY(bounds[3].toDouble() - bounds[1].toDouble() < 30);
        if (link["uri"].toString() == "mailto:test@example.com") ++mailRuns;
    }
    QVERIFY(mailRuns >= 3);
    // Destination resolution compiles far enough to discover an unseen page.
    session->preview.activateLink(0, linkIndex(metadata, "#later"), session->preview.revision(), session->preview.sequence());
    QVERIFY2(wait("lazy later destination", [&] { return session->preview.page() == 1 && session->preview.valid() && !reveals.isEmpty(); }), qPrintable(diagnostics()));
    QVERIFY(reveals.last()[0].toPointF().y() > 0);
    metadata = session->preview.annotationsForPage(1); QVERIFY(linkIndex(metadata, "#first") >= 0);
    session->preview.activateLink(1, linkIndex(metadata, "#first"), session->preview.revision(), session->preview.sequence());
    QVERIFY2(wait("first-page backlink", [&] { return session->preview.page() == 0 && session->preview.valid() && reveals.size() >= 2; }), qPrintable(diagnostics()));
    // Complete the document, then consume its generated reference/TOC data.
    session->preview.setPage(2);
    QVERIFY2(wait("last page image", [&] { return session->preview.page() == 2 && session->preview.valid(); }), qPrintable(diagnostics()));
    // A lazy compiler can stop at the final shipout before reaching EOF.
    // Resolving an absent target must finish the search and return a visible
    // error, rather than leaving navigation pending indefinitely.
    const int missing = linkIndex(session->preview.annotationsForPage(0), "#missing"); QVERIFY(missing >= 0);
    session->preview.activateLink(0, missing, session->preview.revision(), session->preview.sequence());
    QVERIFY2(wait("missing target and EOF", [&] { return session->preview.complete() && !session->preview.busy() && !failures.isEmpty(); }), qPrintable(diagnostics()));
    const auto revision = session->preview.revision(); session->rerunOnce();
    QVERIFY2(wait("auxiliary rerun", [&] { return session->preview.valid() && session->preview.revision() > revision; }), qPrintable(diagnostics()));
    session->preview.setPage(0);
    QVERIFY2(wait("reference and TOC image", [&] { return session->preview.valid(); }), qPrintable(diagnostics()));
    metadata = session->preview.annotationsForPage(0);
    QVERIFY2(linkIndex(metadata, "#cite.foo") >= 0, qPrintable(session->compilerLogOutput()));
    int sectionLinks = 0;
    for (const auto &value : metadata["links"].toArray())
        if (value.toObject()["uri"].toString().startsWith("#section.")) ++sectionLinks;
    QVERIFY(sectionLinks >= 2); // The TOC and the explicit reference use the same section destination.
    session->preview.activateLink(0, linkIndex(metadata, "#cite.foo"), session->preview.revision(), session->preview.sequence());
    QVERIFY2(wait("citation destination", [&] { return session->preview.page() == 2 && session->preview.valid() && reveals.size() >= 3; }), qPrintable(diagnostics()));
    session->preview.setPage(0);
    QVERIFY2(wait("cached first page", [&] { return session->preview.valid(); }), qPrintable(diagnostics()));
    const auto oldRevision = session->preview.revision(), oldSequence = session->preview.sequence();
    const auto oldIndex = linkIndex(session->preview.annotationsForPage(0), "https://example.com/a?x=1&y=2");
    manager.openBuffer(root, QString(source).replace("First page.", "Changed first page."));
    session->preview.activateLink(0, oldIndex, oldRevision, oldSequence); QVERIFY(external.isEmpty());
    QVERIFY2(wait("edited first page", [&] { return session->preview.valid() && session->preview.sequence() > oldSequence; }), qPrintable(diagnostics()));
    manager.shutdown();
}

QTEST_MAIN(LinksTest)
#include "links_test.moc"
