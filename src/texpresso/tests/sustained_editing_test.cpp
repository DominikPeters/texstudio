#include <QtTest>
#include "texpressomanager.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <functional>

// These are correctness experiments, not UI latency benchmarks. Input deadlines
// depend only on the clock: a slow render must not throttle the edit stream.
class SustainedEditingTest : public QObject
{
    Q_OBJECT
private slots:
    void corpusPlans();
    void sustainedEditing_data();
    void sustainedEditing();
};

namespace {
const QStringList anchors = {"early", "middle", "late"};
QString fixture(const QString &relative) { return QString::fromUtf8(TEXPRESSO_SUSTAINED_FIXTURES) + '/' + relative; }
QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QJsonArray plans() { return QJsonDocument::fromJson(readFile(fixture("plans.json"))).array(); }
QString marker(const QString &anchor) { return "%<VARIED-CURSOR:" + anchor + '>'; }
int markerOffset(const QString &source, const QString &anchor) { return source.indexOf(marker(anchor)); }
int lineAt(const QString &source, int offset) { return source.left(offset).count('\n'); }
int columnAt(const QString &source, int offset) { return offset - source.lastIndexOf('\n', offset - 1) - 1; }
bool resolveInput(const QString &source, const QJsonObject &input, int &offset, int &length, QString &insertion)
{
    const int end = markerOffset(source, input["anchor"].toString());
    const auto deleted = input["deleteBefore"].toString();
    offset = end - deleted.size(); length = deleted.size(); insertion = input["text"].toString();
    return end >= 0 && offset >= 0 && source.mid(offset, length) == deleted;
}
bool waitFor(const std::function<bool()> &condition, int timeoutMs = 30000)
{
    QElapsedTimer clock; clock.start();
    while (!condition() && clock.elapsed() < timeoutMs) QTest::qWait(20);
    return condition();
}
QString tool(const QString &name)
{
    auto result = QStandardPaths::findExecutable(name);
    if (!result.isEmpty()) return result;
    return QStandardPaths::findExecutable(name, {"/Library/TeX/texbin", "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"});
}
struct Finalize { std::function<void()> action; ~Finalize() { action(); } };
}

void SustainedEditingTest::corpusPlans()
{
    const auto allPlans = plans();
    QCOMPARE(allPlans.size(), 5);
    for (int pages : {30, 100}) {
        const auto original = QString::fromUtf8(readFile(fixture(QString("varied-%1/varied.tex").arg(pages))));
        QVERIFY(!original.isEmpty());
        QVERIFY(original.contains(QString::fromUtf8("café 😀 章节")));
        QVERIFY(!original.contains("\\newpage")); // Pagination must arise from content.
        QString source = original, beforePaste;
        const auto metadata = QJsonDocument::fromJson(readFile(fixture(QString("varied-%1/fixture.json").arg(pages)))).object();
        for (const auto &anchor : anchors) {
            const auto token = marker(anchor); QCOMPARE(source.count(token), 1);
            const int offset = markerOffset(source, anchor);
            const auto recorded = metadata["anchors"].toObject()[anchor].toObject();
            QCOMPARE(recorded["utf16Offset"].toInt(), offset);
            QCOMPARE(recorded["byteOffset"].toInt(), source.left(offset).toUtf8().size());
            QCOMPARE(recorded["line"].toInt(), lineAt(source, offset));
        }
        for (const auto &value : allPlans) {
            const auto plan = value.toObject();
            const auto before = source;
            if (plan["id"] == "paste-incomplete-repair") beforePaste = source;
            const auto inputs = plan["inputs"].toArray();
            QVERIFY(!inputs.isEmpty());
            for (const auto &item : inputs) {
                int offset, length; QString insertion;
                QVERIFY(resolveInput(source, item.toObject(), offset, length, insertion));
                source.replace(offset, length, insertion);
            }
            const auto anchor = plan["anchor"].toString();
            const int end = markerOffset(source, anchor);
            const auto inserted = plan["expectedText"].toString();
            if (!inserted.isEmpty()) QCOMPARE(source.mid(end - inserted.size(), inserted.size()), inserted);
            if (plan["id"] == "undo-pasted-block") {
                QVERIFY(before.contains("\\subsection{A pasted calculation}"));
                QVERIFY(!source.contains("\\subsection{A pasted calculation}"));
                QCOMPARE(source, beforePaste);
            }
        }
        QVERIFY(source.size() > original.size());
    }
}

void SustainedEditingTest::sustainedEditing_data()
{
    QTest::addColumn<int>("targetPages");
    QTest::newRow("30pages") << 30;
    QTest::newRow("100pages") << 100;
}

void SustainedEditingTest::sustainedEditing()
{
    if (qEnvironmentVariableIntValue("TEXPRESSO_TEST_SUSTAINED") != 1)
        QSKIP("Set TEXPRESSO_TEST_SUSTAINED=1 for the finite 30/100-page source-follow regression");
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    QVERIFY2(!engine.isEmpty(), "TEXPRESSO_TEST_ENGINE must select the bundled engine under test");
    const auto xelatex = tool("xelatex"), synctex = tool("synctex"), pdftotext = tool("pdftotext");
    QVERIFY2(!xelatex.isEmpty() && !synctex.isEmpty() && !pdftotext.isEmpty(), "Install local XeLaTeX, standalone SyncTeX and Poppler pdftotext");
    QFETCH(int, targetPages);
    const auto artifacts = qEnvironmentVariable("TEXPRESSO_SUSTAINED_ARTIFACTS");
    if (!artifacts.isEmpty()) QVERIFY(QDir().mkpath(artifacts));
    QTemporaryDir directory(artifacts.isEmpty() ? QDir::tempPath() + "/texstudio-sustained-XXXXXX"
        : artifacts + QString("/%1pages-XXXXXX").arg(targetPages));
    QVERIFY(directory.isValid()); directory.setAutoRemove(artifacts.isEmpty());
    qInfo().noquote() << "Sustained regression directory:" << directory.path();
    const auto root = directory.filePath("varied.tex");
    QString source = QString::fromUtf8(readFile(fixture(QString("varied-%1/varied.tex").arg(targetPages))));
    QVERIFY(!source.isEmpty()); QVERIFY(writeFile(root, source.toUtf8()));
    TexpressoManager manager(nullptr, engine); manager.openBuffer(root, source); manager.activate(root);
    auto session = manager.current(); QVERIFY(session);
    session->preview.setWidth(720);
    // Emulate a one-page viewport around the selected page. Real widgets send
    // this demand after a source reveal; leaving a previous manual viewport in
    // place would invent an off-screen dependency in this session-only test.
    connect(&session->preview, &TexpressoPreview::updated, session, [session] {
        if (session->preview.valid()) session->preview.setViewportPages({session->preview.page()});
    });
    QJsonArray inputReport, phaseReport, probeReport, commandReport;
    QElapsedTimer experiment; experiment.start();
    const auto commandConnection = connect(&session->preview, &TexpressoPreview::command, session, [&](const QJsonArray &command) {
        const auto verb = command[0].toString();
        if (verb.startsWith("synctex")) commandReport.append(QJsonObject{{"atMs", double(experiment.elapsed())}, {"command", command}});
    });
    bool passed = false;
    const auto engineSha256 = QString::fromLatin1(QCryptographicHash::hash(readFile(engine), QCryptographicHash::Sha256).toHex());
    Finalize save{[&] {
        writeFile(directory.filePath("edited.tex"), source.toUtf8());
        writeFile(directory.filePath("engine.log"), session->engineOutput.toUtf8());
        writeFile(directory.filePath("compiler.log"), session->compilerLogOutput().toUtf8());
        const QJsonObject report{{"schema", 1}, {"passed", passed}, {"targetPages", targetPages},
            {"engine", engine}, {"engineSha256", engineSha256},
            {"sourceSha256", QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex())},
            {"elapsedMs", double(experiment.elapsed())}, {"inputs", inputReport}, {"phases", phaseReport},
            {"probes", probeReport}, {"commands", commandReport},
            {"method", "Qt session edits on fixed deadlines; final independent XeLaTeX, SyncTeX and Poppler page ownership"}};
        writeFile(directory.filePath("result.json"), QJsonDocument(report).toJson());
        disconnect(commandConnection);
    }};
    const auto diagnostics = [&] {
        return QString("page=%1 count=%2 valid=%3 complete=%4 busy=%5 revision=%6 sequence=%7\n")
            .arg(session->preview.page()).arg(session->preview.pageCount()).arg(session->preview.valid())
            .arg(session->preview.complete()).arg(session->preview.busy()).arg(session->preview.revision()).arg(session->preview.sequence())
            + session->status + '\n' + session->previewError + '\n' + session->compilerError + '\n' + session->engineOutput.right(5000);
    };
    const auto settled = [&] { return session->preview.valid() && !session->preview.busy(); };
    QVERIFY2(waitFor([&] { return settled() || session->failed(); }, 60000), qPrintable(diagnostics()));
    QVERIFY2(!session->failed(), qPrintable(diagnostics()));
    const qint64 processId = session->processId(); QVERIFY(processId > 0);

    for (const auto &value : plans()) {
        const auto plan = value.toObject();
        const auto id = plan["id"].toString(), anchor = plan["anchor"].toString();
        const auto inputs = plan["inputs"].toArray();
        session->syncForward(root, lineAt(source, markerOffset(source, anchor)));
        QVERIFY2(waitFor([&] { return settled() || session->failed(); }), qPrintable(diagnostics()));
        const int interval = plan["intervalMs"].toInt();
        QElapsedTimer phaseClock; phaseClock.start();
        QTimer inputTimer; inputTimer.setSingleShot(true); inputTimer.setTimerType(Qt::PreciseTimer);
        int index = 0; QString inputFailure; bool done = false;
        connect(&inputTimer, &QTimer::timeout, &inputTimer, [&] {
            const auto input = inputs[index].toObject();
            int offset, length; QString insertion;
            if (!resolveInput(source, input, offset, length, insertion)) { inputFailure = "Input tail mismatch in " + id; done = true; return; }
            const int firstLine = lineAt(source, offset), firstColumn = columnAt(source, offset);
            const int lastLine = lineAt(source, offset + length), lastColumn = columnAt(source, offset + length);
            source.replace(offset, length, insertion);
            manager.changeBuffer(root, firstLine, firstColumn, lastLine, lastColumn, insertion, source);
            const auto currentAnchor = input["anchor"].toString();
            const int cursor = markerOffset(source, currentAnchor);
            session->syncForward(root, lineAt(source, cursor), false, columnAt(source, cursor));
            session->prepareEdit(root, lineAt(source, cursor), columnAt(source, cursor));
            inputReport.append(QJsonObject{{"phase", id}, {"index", index}, {"scheduledMs", index * interval},
                {"appliedMs", double(phaseClock.elapsed())}, {"sequence", double(session->preview.sequence())},
                {"deletedUtf16", length}, {"insertedUtf16", insertion.size()},
                {"incomplete", input["temporarilyIncomplete"].toBool()}});
            for (const auto &navigation : plan["navigation"].toArray()) {
                const auto nav = navigation.toObject();
                if (nav["afterInput"].toInt() != index + 1) continue;
                if (nav["action"] == "away") {
                    const int away = qMax(0, session->preview.pageCount() - 3);
                    session->preview.setViewportPages({away}, true);
                    session->preview.navigateTo(away);
                } else {
                    session->syncForward(root, lineAt(source, markerOffset(source, nav["anchor"].toString())));
                }
            }
            ++index;
            if (index == inputs.size()) { done = true; return; }
            inputTimer.start(int(qMax<qint64>(0, qint64(index) * interval - phaseClock.elapsed())));
        });
        inputTimer.start(0);
        QVERIFY2(waitFor([&] { return done || session->failed(); }, inputs.size() * interval + 30000), qPrintable(diagnostics()));
        inputTimer.stop();
        QVERIFY2(inputFailure.isEmpty(), qPrintable(inputFailure));
        QCOMPARE(index, inputs.size());
        QVERIFY2(waitFor([&] { return (settled() && session->compilerError.isEmpty()) || session->failed(); }, 60000), qPrintable(diagnostics()));
        QVERIFY2(!session->failed(), qPrintable(diagnostics()));
        QVERIFY2(session->compilerError.isEmpty(), qPrintable(diagnostics()));
        QCOMPARE(session->processId(), processId);
        phaseReport.append(QJsonObject{{"id", id}, {"inputs", inputs.size()}, {"elapsedMs", double(phaseClock.elapsed())},
            {"page", session->preview.page()}, {"sequence", double(session->preview.sequence())}});
        qInfo().noquote() << id << "completed; inputs=" << inputs.size() << "page=" << session->preview.page();
        QVERIFY(writeFile(directory.filePath("source-after-" + id + ".tex"), source.toUtf8()));
    }

    // Build in a separate directory after editing stops. Neither the live VFS,
    // its checkpoints nor its auxiliary files participate in this oracle.
    const auto clean = directory.filePath("clean"); QVERIFY(QDir().mkpath(clean));
    const auto oracleSource = clean + "/varied.tex", pdf = clean + "/varied.pdf";
    QVERIFY(writeFile(oracleSource, source.toUtf8()));
    QString commandFailure;
    const auto run = [&](const QString &program, const QStringList &args, const QString &label) {
        QProcess process; process.setWorkingDirectory(clean); process.setProcessEnvironment(TexpressoProcess::texliveEnvironment());
        process.start(program, args);
        const bool started = process.waitForStarted(5000);
        const bool finished = started && process.waitForFinished(60000);
        const auto output = process.readAllStandardOutput(), error = process.readAllStandardError();
        writeFile(directory.filePath(label + ".stdout.log"), output); writeFile(directory.filePath(label + ".stderr.log"), error);
        if (!finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            commandFailure = label + ": " + QString::fromUtf8(error + output).right(5000);
        return output;
    };
    for (int pass = 1; pass <= 2; ++pass) {
        run(xelatex, {"-no-shell-escape", "-interaction=nonstopmode", "-halt-on-error", "-synctex=1", "varied.tex"}, QString("xelatex-%1").arg(pass));
        QVERIFY2(commandFailure.isEmpty(), qPrintable(commandFailure));
    }
    auto pdfPages = QString::fromUtf8(run(pdftotext, {"-layout", "-enc", "UTF-8", pdf, "-"}, "pdftotext")).split('\f');
    QVERIFY2(commandFailure.isEmpty(), qPrintable(commandFailure));
    if (!pdfPages.isEmpty() && pdfPages.last().trimmed().isEmpty()) pdfPages.removeLast();
    QVERIFY(pdfPages.size() >= int(targetPages * 0.85));
    QVERIFY(pdfPages.size() <= int(targetPages * 1.25));
    // The live compiler is lazy: demand the independently known final page
    // before comparing counts, rather than accepting partial pagination.
    session->preview.navigateTo(pdfPages.size() - 1);
    QVERIFY2(waitFor([&] { return settled() || session->failed(); }), qPrintable(diagnostics()));
    QVERIFY2(!session->failed(), qPrintable(diagnostics()));
    QCOMPARE(session->preview.pageCount(), pdfPages.size());
    for (const auto &anchor : anchors) {
        const auto visiblePrefix = "\\noindent \\texttt{" + anchor.toUpper() + '-';
        const int visibleOffset = source.indexOf(visiblePrefix); QVERIFY(visibleOffset >= 0);
        const int visibleLine = lineAt(source, visibleOffset);
        const auto query = QString("%1:1:%2").arg(visibleLine + 1).arg(oracleSource);
        const auto result = QString::fromUtf8(run(synctex, {"view", "-i", query, "-o", pdf}, "synctex-" + anchor));
        QVERIFY2(commandFailure.isEmpty(), qPrintable(commandFailure));
        QSet<int> queryPages;
        auto matches = QRegularExpression("(?:^|\\n)Page:([0-9]+)").globalMatch(result);
        while (matches.hasNext()) queryPages.insert(matches.next().captured(1).toInt() - 1);
        QVERIFY2(!queryPages.isEmpty(), qPrintable(result));
        const auto phrase = anchor.toUpper() + '-';
        int owningPage = -1, occurrences = 0;
        for (int page = 0; page < pdfPages.size(); ++page) {
            if (pdfPages[page].contains(phrase)) { owningPage = page; ++occurrences; }
        }
        QCOMPARE(occurrences, 1); QVERIFY(queryPages.contains(owningPage));
        // Start elsewhere so a previously satisfied search cannot hide a
        // missing/new source routing result. Record only a fresh matching frame.
        session->preview.setPage(owningPage == 0 ? 1 : 0);
        session->syncForward(root, visibleLine);
        QVERIFY2(waitFor([&] { return (settled() && session->preview.page() == owningPage) || session->failed(); }), qPrintable(diagnostics()));
        QVERIFY2(!session->failed(), qPrintable(diagnostics()));
        QCOMPARE(session->preview.page(), owningPage);
        const auto liveRevision = session->preview.revision(), liveSequence = session->preview.sequence();
        QJsonArray oraclePages; for (int page : queryPages) oraclePages.append(page);
        probeReport.append(QJsonObject{{"anchor", anchor}, {"line1", visibleLine + 1}, {"livePage", session->preview.page()},
            {"textPage", owningPage}, {"synctexPages", oraclePages}, {"revision", double(liveRevision)}, {"sequence", double(liveSequence)}});
    }
    passed = true;
}

QTEST_MAIN(SustainedEditingTest)
#include "sustained_editing_test.moc"
