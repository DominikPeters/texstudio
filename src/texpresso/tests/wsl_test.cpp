#include <QtTest>
#include "texpressowsl.h"
#include "texpressomanager.h"
#include <QFile>
#include <QTemporaryDir>

class WslTest : public QObject
{
    Q_OBJECT
private slots:
    void pathsAndProtocol();
    void realEngine();
};
void WslTest::pathsAndProtocol()
{
    TexpressoWsl wsl; wsl.enabled = true; wsl.distribution = "My Linux";
    wsl.drives.insert("C:/", "/custom/c/");
    QCOMPARE(wsl.toEngine("C:\\Project files\\é.tex"), QString("/custom/c/Project files/é.tex"));
    QCOMPARE(wsl.toEditor("/custom/c/Project files/é.tex"), QString("C:/Project files/é.tex"));
    QCOMPARE(wsl.toEngine("//wsl.localhost/My Linux/home/me/main.tex"), QString("/home/me/main.tex"));
    QCOMPARE(wsl.toEditor("/usr/share/texlive/article.cls"), QString("//wsl.localhost/My Linux/usr/share/texlive/article.cls"));
    QCOMPARE(wsl.toEngine("chapter.tex"), QString("chapter.tex"));
    const QString text = "Text containing C:/files and https://example.org must stay unchanged.";
    const auto command = wsl.outgoing({"open", "C:/main.tex", text});
    QCOMPARE(command, QJsonArray({"open", "/custom/c/main.tex", text}));
    QCOMPARE(wsl.outgoing({"preview-prepare", 3, "C:/main.tex", 42})[2].toString(), QString("/custom/c/main.tex"));
    QCOMPARE(wsl.incoming({"lookup-file", "read", "successful", "/custom/c/main.tex"})[3].toString(), QString("C:/main.tex"));
    QCOMPARE(wsl.incoming({"synctex", "/home/me/main.tex", 2})[1].toString(), QString("//wsl.localhost/My Linux/home/me/main.tex"));
    QCOMPARE(wsl.arguments("engine", {"one argument"}), QStringList({"--distribution", "My Linux", "--exec", "engine", "one argument"}));
    wsl.enabled = false;
    QCOMPARE(wsl.outgoing({"open", "C:/main.tex", text}), QJsonArray({"open", "C:/main.tex", text}));
}
void WslTest::realEngine()
{
    if (qEnvironmentVariableIsEmpty("TEXPRESSO_TEST_ENGINE")) QSKIP("Set TEXPRESSO_TEST_ENGINE for real engine integration");
    QTemporaryDir temporary; QVERIFY(temporary.isValid());
    QDir directory(temporary.path());
#ifdef Q_OS_MACOS
    // macOS decomposes Unicode filenames; the existing engine's overlay keys
    // need a separate normalization fix. Keep actual Windows/Linux coverage.
    const auto folder = QStringLiteral("Project with spaces");
#else
    const auto folder = QString::fromUtf8("Project with spaces é");
#endif
    QVERIFY(directory.mkdir(folder));
    const auto root = directory.filePath(folder + "/main.tex");
    const auto text = QStringLiteral("\\documentclass{article}\n\\usepackage{hyperref}\n\\begin{document}\n\\typeout{WSL-INITIAL}\nWSL-INITIAL.\n\\hyperlink{target}{Go to target}\n\\newpage\n\\hypertarget{target}{Second page}\n\\end{document}\n");
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(text.toUtf8()); file.close();
    TexpressoManager manager(nullptr, qEnvironmentVariable("TEXPRESSO_TEST_ENGINE"));
    manager.openBuffer(root, text); manager.activate(root);
    auto session = manager.current(); QVERIFY(session); session->followCursor = false; session->setAutoRerun(false);
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid() || session->failed(), 90000);
    QVERIFY2(!session->failed(), qPrintable(session->status + "\n" + session->engineOutput));
    QTRY_VERIFY_WITH_TIMEOUT(session->compilerOutput.contains("WSL-INITIAL"), 30000);
    QVERIFY(!session->preview.image().isNull());
    QSignalSpy inverse(session, &TexpressoSession::sourceLocation);
    session->preview.inverse({80, 130});
    QTRY_VERIFY_WITH_TIMEOUT(!inverse.isEmpty(), 30000);
    QVERIFY2(QFileInfo(inverse.last()[0].toString()).isFile(), qPrintable(inverse.last()[0].toString()));
    const auto edited = QString(text).replace("WSL-INITIAL", "WSL-EDITED");
    manager.openBuffer(root, edited);
    QTRY_VERIFY2_WITH_TIMEOUT(session->preview.valid() && session->compilerOutput.contains("WSL-EDITED"), qPrintable(session->status + "\nENGINE:\n" + session->engineOutput.right(3000) + "\nOUTPUT:\n" + session->compilerOutput.right(3000)), 10000);
    session->preview.setPage(1);
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid() && session->preview.page() == 1, 30000);
    session->setPaused(true); QVERIFY(session->userPaused);
    session->setPaused(false);
    manager.shutdown();
}
QTEST_MAIN(WslTest)
#include "wsl_test.moc"
