#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QBuffer>
#include <cmath>
#include <QPushButton>
#include <QJsonDocument>
#include "texpressotikz.h"
#include "texpressowidget.h"

class TikzTest : public QObject
{
    Q_OBJECT
private slots:
    void lexicalSourceRanges();
    void editorDiscovery();
    void launchArguments();
    void protocolValidation();
    void hoverAndStaleTargets();
    void realFigures();
    void providedPaper();
};
static void write(const QString &path, const QByteArray &bytes)
{
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size()));
}
void TikzTest::lexicalSourceRanges()
{
    const auto text = QString::fromUtf8(R"TEX(% \begin{tikzpicture} commented
\verb|\begin{tikzpicture}|
\begin{verbatim}
\begin{tikzpicture} ignored
\end{verbatim}
\begin{tikzpicture}
\node {\begin{tikzpicture}\end{tikzpicture}};
\end{tikzpicture}
\begin{tikzpicture}\end{tikzpicture}
)TEX");
    const auto figures = TexpressoTikz::sourceFigures(text);
    QCOMPARE(figures.size(), 2);
    QCOMPARE(figures[0].line, 6); QCOMPARE(figures[0].endLine, 9);
    QVERIFY(text.mid(figures[0].offset, figures[0].endOffset - figures[0].offset).endsWith("\\end{tikzpicture}"));
    QCOMPARE(figures[1].line, 9); QCOMPARE(figures[1].endLine, 10);
    QVERIFY(TexpressoTikz::sourceFigures("\\begin{tikzpicture} unfinished").isEmpty());
    QVERIFY(TexpressoTikz::findEditor("/nonexistent/tikz-editor").isEmpty());
    const auto wrapped = TexpressoTikz::sourceFigures("\\scalebox{.5}{\n\\begin{tikzpicture}\n\\end{tikzpicture}\n}");
    QCOMPARE(TexpressoTikz::resolveFigure(wrapped, 4, 5).line, 2);
    const auto ambiguous = TexpressoTikz::sourceFigures("\\scalebox{.5}{\n\\begin{tikzpicture}\\end{tikzpicture}\n\\begin{tikzpicture}\\end{tikzpicture}}");
    QCOMPARE(TexpressoTikz::resolveFigure(ambiguous, 3, 4).line, 0);
}
void TikzTest::editorDiscovery()
{
    QTemporaryDir temp; QVERIFY(temp.isValid());
    const auto executable = temp.filePath("editor with spaces");
    write(executable, "fixture; never launched\n");
    QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QCOMPARE(TexpressoTikz::findEditor(executable), executable);
    QVERIFY(TexpressoTikz::findEditor(temp.path()).isEmpty());
#ifdef Q_OS_MACOS
    const auto bundle = temp.filePath("Renamed TikZ Editor.app");
    QVERIFY(QDir().mkpath(bundle + "/Contents/MacOS"));
    const auto app = bundle + "/Contents/MacOS/app";
    write(app, "fixture; never launched\n");
    QVERIFY(QFile::setPermissions(app, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    write(bundle + "/Contents/Info.plist", R"PLIST(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>org.texstudio.tikz-discovery-test</string>
<key>CFBundlePackageType</key><string>APPL</string><key>CFBundleExecutable</key><string>app</string></dict></plist>)PLIST");
    QCOMPARE(TexpressoTikz::findEditor(bundle), app);
    QVERIFY(QFile::setPermissions(app, QFile::ReadOwner | QFile::WriteOwner));
    QVERIFY(TexpressoTikz::findEditor(bundle).isEmpty());
    // Opt-in read-only check of the actual installed app. No editor is launched.
    const auto installed = qEnvironmentVariable("TEXPRESSO_TEST_TIKZ_EDITOR");
    if (!installed.isEmpty()) {
        const auto found = TexpressoTikz::findEditor();
        qInfo() << "Detected installed TikZ Editor:" << found;
        QCOMPARE(QFileInfo(found).canonicalFilePath(), QFileInfo(installed).canonicalFilePath());
    }
#endif
}
void TikzTest::launchArguments()
{
    QTemporaryDir temp; QVERIFY(temp.isValid());
    const auto path = temp.filePath("café $(literal) figure.tex"); write(path, "% fixture\n");
    QVERIFY(TexpressoTikz::launch(QCoreApplication::applicationFilePath(), path, 284));
    QFile record(path + ".argv");
    QTRY_VERIFY_WITH_TIMEOUT(record.exists() && record.size() > 0, 5000);
    QVERIFY(record.open(QIODevice::ReadOnly));
    const auto arguments = QJsonDocument::fromJson(record.readAll()).array();
    QCOMPARE(arguments.size(), 2); QCOMPARE(arguments[0].toString(), QString("--source-line=284"));
    // macOS normalizes filesystem names to decomposed Unicode. The spelling
    // may change, while the literal shell characters and file identity must not.
    QCOMPARE(arguments[1].toString().normalized(QString::NormalizationForm_C), path.normalized(QString::NormalizationForm_C));
    QCOMPARE(QFileInfo(arguments[1].toString()).canonicalFilePath(), QFileInfo(path).canonicalFilePath());
}
void TikzTest::protocolValidation()
{
    QJsonObject figure{{"bounds", QJsonArray{20, 30, 100, 80}}, {"file", "café main.tex"}, {"line", 4}, {"endLine", 8}};
    QJsonObject annotations{{"links", QJsonArray{}}, {"destinations", QJsonArray{}}, {"figures", QJsonArray{figure}}};
    QJsonObject header{{"requestId", 1}, {"revision", 1}, {"sequence", 0}, {"page", 0}, {"width", 20}, {"height", 30},
        {"mime", "image/png"}, {"byteLength", 20}, {"bounds", QJsonArray{0, 0, 200, 300}}, {"annotations", annotations}};
    QVERIFY(TexpressoProtocol::validImage(header));
    for (const auto &pair : QList<QPair<QString, QJsonValue>>{{"line", 0}, {"line", 1.5}, {"endLine", 4},
             {"file", "bad\nfile"}, {"bounds", QJsonArray{0, 0, -1, 2}}}) {
        auto invalid = figure; invalid[pair.first] = pair.second;
        auto metadata = annotations; metadata["figures"] = QJsonArray{invalid}; header["annotations"] = metadata;
        QVERIFY(!TexpressoProtocol::validImage(header));
    }
}
void TikzTest::hoverAndStaleTargets()
{
    PDFDocumentConfig config; QScrollArea area; TexpressoCanvas canvas(&config, &area);
    canvas.resize(200, 300); canvas.pixels = QImage(200, 300, QImage::Format_RGB32); canvas.pixels.fill(Qt::white);
    canvas.pageBounds = QRectF(0, 0, 200, 300); canvas.scale = 1; canvas.fresh = true;
    canvas.setAnnotations(2, 7, 9, {});
    canvas.setTikzFigures({QJsonObject{{"bounds", QJsonArray{20, 30, 100, 80}}, {"index", 4}}}, true);
    QCOMPARE(canvas.figureAt({40, 50}), 0); QCOMPARE(canvas.figureAt({5, 5}), -1);
    QSignalSpy activated(&canvas, &TexpressoCanvas::tikzActivated);
    canvas.activateFigure(0); QCOMPARE(activated.size(), 1);
    QCOMPARE(activated[0], QVariantList({2, 4, qint64(7), qint64(9)}));
    canvas.fresh = false; canvas.activateFigure(0); QCOMPARE(activated.size(), 1);
    canvas.fresh = true; canvas.setTikzFigures({}, true); canvas.activateFigure(0); QCOMPARE(activated.size(), 1);
    QVERIFY(!canvas.figurePopup->isVisible());
    canvas.setTikzFigures({QJsonObject{{"bounds", QJsonArray{20, 30, 100, 80}}, {"index", 4}}}, true);
    canvas.show(); QTest::mouseMove(&canvas, {40, 50});
    QTRY_VERIFY_WITH_TIMEOUT(canvas.figurePopup->isVisible(), 1500);
    auto button = canvas.figurePopup->findChild<QPushButton *>("texpressoEditTikz"); QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(activated.size(), 2);
    QVERIFY(!canvas.figurePopup->isVisible());
}
void TikzTest::realFigures()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE", TexpressoProcess::bundledEngine());
    if (!QFileInfo::exists(engine)) QSKIP("Set TEXPRESSO_TEST_ENGINE to the built engine");
    QTemporaryDir temp; QVERIFY(temp.isValid());
    const auto path = temp.filePath("café figures.tex");
    const QByteArray source = R"TEX(\documentclass{article}
\usepackage{tikz}
\usepackage{graphicx}
\newcommand{\badge}{\tikz{\fill (0,0) circle (2pt);}}
\newcommand{\macrofigure}{\begin{tikzpicture}\draw (0,0) rectangle (2,2);\end{tikzpicture}}
\begin{document}
Text \badge. \macrofigure
\begin{tikzpicture}
\draw[red,line width=2pt] (0,0) rectangle (3,2);
\node at (1,1) {Nested \badge};
\end{tikzpicture}
\scalebox{0.5}{\begin{tikzpicture}\fill[blue] (0,0) rectangle (3,2);\end{tikzpicture}}
\rotatebox{30}{\begin{tikzpicture}\fill[green] (0,0) rectangle (3,2);\end{tikzpicture}}
\begin{tikzpicture}\fill (0,0) rectangle (0.2,0.2);\end{tikzpicture}
\input{included}
\scalebox{1}{
\begin{tikzpicture}\fill (0,0) rectangle (2,2);\end{tikzpicture}
\macrofigure}
\end{document}
)TEX";
    write(path, source);
    write(temp.filePath("included.tex"), "\\begin{tikzpicture}\\draw (0,0) rectangle (2,2);\\end{tikzpicture}\n");
    TexpressoSession session(path, engine, {{TexpressoSession::identity(path), QString::fromUtf8(source)}});
    session.setActive(true); session.start();
    QTRY_VERIFY_WITH_TIMEOUT(!session.compilerError.isEmpty() || (session.preview.valid() && session.preview.complete()), 90000);
    QVERIFY2(session.compilerError.isEmpty(), qPrintable(session.compilerError + "\n" + session.compilerOutput.right(5000) + session.engineOutput.right(2000)));
    const auto all = session.preview.annotationsForPage(0)["figures"].toArray();
    QCOMPARE(all.size(), 9); // plus two ambiguous instances in one collected argument
    const auto figures = session.tikzFigures(0);
    QCOMPARE(figures.size(), 4); // outer/scaled/rotated/input; macros and tiny pictures excluded
    QSet<int> lines;
    QJsonObject outer, scaled;
    for (const auto &value : figures) {
        const auto figure = value.toObject();
        if (figure["file"].toString() == TexpressoSession::identity(path)) lines.insert(figure["line"].toInt());
        if (figure["line"] == 8) outer = figure;
        if (figure["line"] == 12) scaled = figure;
    }
    QCOMPARE(lines, QSet<int>({8, 12, 13}));
    const auto a = outer["bounds"].toArray(), b = scaled["bounds"].toArray();
    QVERIFY(a.size() == 4 && b.size() == 4);
    QVERIFY(std::abs((a[2].toDouble() - a[0].toDouble()) - 87.03) < .1);
    QVERIFY(std::abs((b[2].toDouble() - b[0].toDouble()) - 42.52) < .1);
    const auto target = session.tikzFigure(0, outer["index"].toInt(), session.preview.revision(), session.preview.sequence());
    QVERIFY(!target.isEmpty());
    QVERIFY(session.tikzFigure(0, outer["index"].toInt(), session.preview.revision() - 1, session.preview.sequence()).isEmpty());
    session.replaceBuffer(path, QString::fromUtf8(source).replace("rectangle (3,2)", "rectangle (4,2)"));
    QVERIFY(session.tikzFigure(0, outer["index"].toInt(), session.preview.revision(), session.preview.sequence() - 1).isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(session.preview.valid() && session.preview.complete(), 30000);
    QCOMPARE(session.tikzFigures(0).size(), 4);
    session.stop(); QVERIFY(session.tikzFigures(0).isEmpty());
}
void TikzTest::providedPaper()
{
    const auto paper = qEnvironmentVariable("TEXPRESSO_TEST_TIKZ_PAPER");
    if (paper.isEmpty()) QSKIP("Set TEXPRESSO_TEST_TIKZ_PAPER for the copied-paper regression");
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE", TexpressoProcess::bundledEngine());
    QTemporaryDir temp; QVERIFY(temp.isValid());
    QFile input(paper); QVERIFY(input.open(QIODevice::ReadOnly));
    const auto original = QString::fromUtf8(input.readAll()).split('\n');
    QVERIFY(original.size() > 310);
    // A focused excerpt keeps the paper's actual nested macros and first large
    // diagram, at their original source lines. Its old ACM class currently also
    // fails ordinary XeLaTeX with duplicate c@conjecture; that unrelated class
    // error must not obscure figure hit-testing. No original files are changed.
    QStringList excerpt;
    for (int i = 0; i < 312; ++i) excerpt.append(QString());
    excerpt[0] = "\\documentclass{article}\\usepackage{tikz,graphicx,xcolor,ifthen}\\usetikzlibrary{positioning}";
    for (int line = 70; line <= 167; ++line) excerpt[line - 1] = original[line - 1];
    excerpt[240] = "\\begin{document}";
    excerpt[241] = "Inline symbols: \\alternative{a} \\borderalternative{b} \\xmark \\cmark \\ExternalLink.";
    excerpt[242] = "\\weakorder{{a,b},{c},{d}}";
    excerpt[281] = "\\noindent\\scalebox{0.7}{";
    for (int line = 284; line <= 308; ++line) excerpt[line - 1] = original[line - 1];
    excerpt[308] = "}";
    excerpt[310] = "\\end{document}";
    const auto copy = temp.filePath("paper-figures.tex");
    write(copy, excerpt.join('\n').toUtf8());
    TexpressoSession session(copy, engine, {}); session.setActive(true); session.start();
    session.preview.lastPage();
    QTRY_VERIFY_WITH_TIMEOUT(!session.compilerError.isEmpty() || session.preview.complete(), 240000);
    QVERIFY2(session.compilerError.isEmpty(), qPrintable(session.compilerError + "\n" + session.compilerOutput.right(5000) + session.engineOutput.right(2000)));
    const int count = session.preview.pageCount();
    QVERIFY(count >= 1);
    int targets = 0, nestedExample = 0;
    for (int page = 0; page < count; ++page) {
        session.preview.setPage(page);
        QTRY_VERIFY_WITH_TIMEOUT(session.preview.pageValid(page), 30000);
        for (const auto &value : session.tikzFigures(page)) {
            const auto figure = value.toObject(); ++targets;
            QVERIFY(figure["line"].toInt() > 200); // no preamble macro definitions
            if (figure["line"].toInt() == 284) ++nestedExample;
        }
    }
    QCOMPARE(nestedExample, 1); QCOMPARE(targets, 1);
    qInfo() << "Copied-paper pages/eligible TikZ figures:" << count << targets;
    session.stop();
}
int main(int argc, char **argv)
{
    // A disposable launch recipient exercises real argv delivery without ever
    // launching the installed editor or opening a visible app/window.
    if (argc == 3 && QByteArray(argv[1]).startsWith("--source-line=")) {
        QCoreApplication application(argc, argv);
        const auto args = application.arguments();
        QFile record(args[2] + ".argv");
        if (!record.open(QIODevice::WriteOnly)) return 1;
        record.write(QJsonDocument(QJsonArray{args[1], args[2]}).toJson());
        return 0;
    }
    QApplication application(argc, argv);
    TikzTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tikz_test.moc"
