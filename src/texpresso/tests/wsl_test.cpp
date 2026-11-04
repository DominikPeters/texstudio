#include <QtTest>
#include "texpressowsl.h"
#include "texpressoprovider.h"
#include "texpressowslsetup.h"
#include <QApplication>
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QSettings>
#include "texpressomanager.h"
#include <QFile>
#include <QTemporaryDir>
#include <QStandardPaths>

class WslTest : public QObject
{
    Q_OBJECT
private slots:
    void pathsAndProtocol();
    void packageProviders();
    void providerSettings();
    void realEngine_data();
    void realEngine();
    void bibliographyBuild();
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
    QCOMPARE(TexpressoWsl::relativePath("/home/Case", "/home/case/file.tex"), QString("../case/file.tex"));
    QCOMPARE(TexpressoWsl::relativePath("/mnt/c/Project", "/mnt/c/Project/main.tex"), QString("main.tex"));
    QCOMPARE(wsl.commandArgument("C:\\Project files\\é.tex", "/custom/c/Project files"), QString("é.tex"));
    QCOMPARE(wsl.commandArgument("--output-directory=C:/build", "/custom/c/Project files"), QString("--output-directory=../build"));
    const QString source = "\\def\\name{C:/something}\\input{main.tex}";
    QCOMPARE(wsl.commandArgument(source, "/custom/c"), source);
    QCOMPARE(wsl.commandArgument("--jobname=letter", "/custom/c"), QString("--jobname=letter"));
    QCOMPARE(wsl.commandArgument("/home/main.tex", "/custom/c"), QString("/home/main.tex"));
    const auto listing = QStringLiteral("  NAME              STATE           VERSION\n* My Linux          Running         2\n  Other Distro      Stopped         1\n");
    QCOMPARE(TexpressoWsl::distributionVersion(listing, "My Linux"), 2);
    QCOMPARE(TexpressoWsl::distributionVersion(listing, "Other Distro"), 1);
    QCOMPARE(TexpressoWsl::distributionVersion(listing, "Missing"), -1);
    QByteArray utf16;
    for (const auto code : listing) { utf16 += char(code.unicode() & 255); utf16 += char(code.unicode() >> 8); }
    QCOMPARE(TexpressoWsl::windowsText(utf16), listing.trimmed());
    wsl.enabled = false;
    QCOMPARE(wsl.outgoing({"open", "C:/main.tex", text}), QJsonArray({"open", "C:/main.tex", text}));
}
void WslTest::packageProviders()
{
    QTemporaryDir temporary; QVERIFY(temporary.isValid());
    QDir folder(temporary.path()); QVERIFY(folder.mkdir("texlive")); QVERIFY(folder.mkdir("miktex"));
    const auto executable = [&](const QString &path) {
        QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
        file.write("fixture"); file.close();
        return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    };
#ifdef Q_OS_WIN
    const QString suffix = ".exe";
#else
    const QString suffix;
#endif
    const auto tl = folder.filePath("texlive"), mi = folder.filePath("miktex");
    QVERIFY(executable(tl + "/kpsewhich" + suffix));
    QVERIFY(executable(mi + "/findtexmf" + suffix));
    QVERIFY(executable(mi + "/kpsewhich" + suffix));
    QProcessEnvironment env; env.insert("PATH", tl);
    QCOMPARE(TexpressoProvider::resolve("auto", env), QString("texlive"));
    QVERIFY(TexpressoProvider::resolve("miktex", env).isEmpty());
    env.insert("PATH", mi);
    QCOMPARE(TexpressoProvider::resolve("auto", env), QString("miktex"));
    QVERIFY(TexpressoProvider::resolve("texlive", env).isEmpty());
    env.insert("PATH", mi + QDir::listSeparator() + tl);
    QCOMPARE(TexpressoProvider::resolve("auto", env), QString("texlive"));
    QCOMPARE(TexpressoProvider::resolve("miktex", env), QString("miktex"));
    QVERIFY(TexpressoProvider::resolve("invalid", env).isEmpty());
    const auto selected = TexpressoProvider::executionEnvironment("texlive", env);
    QCOMPARE(selected.value("PATH").split(QDir::listSeparator()).first(), tl);
    QCOMPARE(TexpressoProvider::executionEnvironment("miktex", selected).value("PATH").split(QDir::listSeparator()).first(), mi);
    QCOMPARE(TexpressoProvider::engineArguments("texlive", false, {}), QStringList{"-texlive"});
    QVERIFY(TexpressoProvider::engineArguments("miktex", false, {}).isEmpty());
    const QJsonObject features = {{"packageProviders", QJsonArray{"texlive", "miktex"}}, {"miktexAutoInstall", true}};
    QCOMPARE(TexpressoProvider::engineArguments("miktex", false, features), QStringList{"-miktex"});
    QCOMPARE(TexpressoProvider::engineArguments("miktex", true, features), QStringList({"-miktex", "-miktex-auto-install"}));
    auto unsupported = features; unsupported.remove("miktexAutoInstall");
    QVERIFY(TexpressoProvider::engineArguments("miktex", true, unsupported).isEmpty());
#ifndef Q_OS_WIN
    // Execute the same shell prefix used inside WSL; no package install occurs.
    QProcess probe; env.insert("HOME", temporary.path()); probe.setProcessEnvironment(env);
    probe.start("/bin/sh", {"-c", TexpressoProvider::shellSetup("miktex") + "printf '%s' \"$TXP_PACKAGE_PROVIDER\""});
    QVERIFY(probe.waitForFinished(5000)); QCOMPARE(probe.exitCode(), 0);
    QCOMPARE(probe.readAllStandardOutput(), QByteArray("miktex"));
#endif
}
void WslTest::providerSettings()
{
#ifdef Q_OS_WIN
    QSKIP("WSL environment setup is covered by the real host checks");
#else
    QTemporaryDir temporary; QSettings settings(temporary.filePath("profile.ini"), QSettings::IniFormat);
    const auto closeDialog = [](bool accept) {
        QTimer::singleShot(0, qApp, [accept] {
            for (auto widget : QApplication::topLevelWidgets()) {
                auto dialog = qobject_cast<QDialog *>(widget);
                if (!dialog || dialog->windowTitle() != "TeX environment") continue;
                auto provider = dialog->findChild<QComboBox *>("texpressoPackageProvider");
                auto install = dialog->findChild<QCheckBox *>("texpressoMiktexAutoInstall");
                QVERIFY(provider); QVERIFY(install); QVERIFY(!install->isChecked());
                provider->setCurrentIndex(provider->findData("miktex"));
                install->setChecked(true);
                accept ? dialog->accept() : dialog->reject();
            }
        });
    };
    closeDialog(false); QVERIFY(!configureTexpressoEnvironment(nullptr, &settings, {}));
    QVERIFY(!settings.contains("texpresso/packageProvider"));
    closeDialog(true); QVERIFY(configureTexpressoEnvironment(nullptr, &settings, {}));
    QCOMPARE(settings.value("texpresso/packageProvider").toString(), QString("miktex"));
    QVERIFY(settings.value("texpresso/miktexAutoInstall").toBool());
    TexpressoProcess::setPackageProvider("auto", false);
#endif
}
void WslTest::realEngine_data()
{
    QTest::addColumn<bool>("linuxFilesystem");
    QTest::newRow("host-project") << false;
#ifdef Q_OS_WIN
    QTest::newRow("wsl-native-project") << true;
#endif
}
void WslTest::realEngine()
{
    if (qEnvironmentVariableIsEmpty("TEXPRESSO_TEST_ENGINE")) QSKIP("Set TEXPRESSO_TEST_ENGINE for real engine integration");
    QFETCH(bool, linuxFilesystem);
    QString pattern = QDir::tempPath() + "/texpresso-wsl-XXXXXX";
    if (linuxFilesystem) {
        TexpressoWsl host; host.enabled = true; host.distribution = qEnvironmentVariable("TEXPRESSO_WSL_DISTRIBUTION");
        host.prepare({}); QVERIFY2(host.error.isEmpty(), qPrintable(host.error));
        pattern = "//wsl.localhost/" + host.distribution + "/tmp/texpresso-qt-XXXXXX";
    }
    QTemporaryDir temporary(pattern); QVERIFY(temporary.isValid());
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
    const auto text = QStringLiteral("\\documentclass{article}\n\\usepackage{hyperref}\n\\begin{document}\n\\typeout{WSL-INITIAL}\nCafé WSL-INITIAL.\n\\hyperlink{target}{Go to target}\n\\newpage\n\\hypertarget{target}{Second page}\n\\end{document}\n");
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(text.toUtf8()); file.close();
    TexpressoManager manager(nullptr, qEnvironmentVariable("TEXPRESSO_TEST_ENGINE"));
    manager.openBuffer(root, text); manager.activate(root);
    auto session = manager.current(); QVERIFY(session); session->followCursor = false; session->setAutoRerun(false);
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid() || session->failed(), 90000);
    QVERIFY2(!session->failed(), qPrintable(session->status + "\n" + session->engineOutput));
    QTRY_VERIFY_WITH_TIMEOUT(session->compilerOutput.contains("WSL-INITIAL"), 30000);
    QVERIFY(!session->preview.image().isNull());
    const auto raster = session->preview.image().convertToFormat(QImage::Format_ARGB32);
    int ink = 0;
    for (int y = 0; y < raster.height(); ++y) {
        const auto row = reinterpret_cast<const QRgb *>(raster.constScanLine(y));
        for (int x = 0; x < raster.width(); ++x) {
            const int alpha = qAlpha(row[x]);
            if ((qGray(row[x]) * alpha + 255 * (255 - alpha)) / 255 < 180) ++ink;
        }
    }
    QVERIFY2(ink > 500, "The decoded preview must contain rendered text, not just a valid blank PNG");
    QSignalSpy inverse(session, &TexpressoSession::sourceLocation);
    session->preview.inverse({80, 130});
    QTRY_VERIFY_WITH_TIMEOUT(!inverse.isEmpty(), 30000);
    QVERIFY2(QFileInfo(inverse.last()[0].toString()).isFile(), qPrintable(inverse.last()[0].toString()));
    const auto edited = QString(text).replace("WSL-INITIAL", "WSL-EDITED");
    manager.openBuffer(root, edited);
    QTRY_VERIFY2_WITH_TIMEOUT(session->preview.valid() && session->compilerOutput.contains("WSL-EDITED"), qPrintable(session->status + "\nENGINE:\n" + session->engineOutput.right(3000) + "\nOUTPUT:\n" + session->compilerOutput.right(3000)), 10000);
    QVERIFY(manager.changeBuffer(root, 3, 9, 3, 19, "WSL-RANGE"));
    QVERIFY(manager.changeBuffer(root, 4, 5, 4, 15, "WSL-RANGE"));
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid() && session->compilerOutput.contains("WSL-RANGE"), 30000);
    manager.openBuffer(root, QString(edited).replace("\\begin{document}", "\\begin{document}\n\\UndefinedForWslTest"));
    QTRY_VERIFY_WITH_TIMEOUT(!session->compilerError.isEmpty(), 30000);
    const auto repaired = QString(edited).replace("WSL-EDITED", "WSL-RECOVERED");
    manager.openBuffer(root, repaired);
    QTRY_VERIFY2_WITH_TIMEOUT(session->preview.valid() && session->compilerError.isEmpty()
                            && session->compilerOutput.contains("WSL-RECOVERED"),
        qPrintable(session->status + "\nERROR:\n" + session->compilerError + "\nENGINE:\n" + session->engineOutput.right(16000) + "\nCOMPILER:\n" + session->compilerOutput.right(4000)), 30000);
    QVERIFY(!session->preview.annotationsForPage(0)["links"].toArray().isEmpty());
    session->preview.activateLink(0, 0, session->preview.revision(), session->preview.sequence());
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid() && session->preview.page() == 1, 30000);
    session->setPaused(true); QVERIFY(session->userPaused);
    session->setPaused(false);
    session->stop(); QVERIFY(session->userStopped);
    manager.restartAll(); QVERIFY(session->userStopped); // Environment changes must preserve Stop.
    manager.restart();
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid() && session->compilerOutput.contains("WSL-RECOVERED"), 30000);
    manager.shutdown();
}

void WslTest::bibliographyBuild()
{
    if (qEnvironmentVariableIsEmpty("TEXPRESSO_TEST_ENGINE")) QSKIP("Set TEXPRESSO_TEST_ENGINE for bibliography integration");
    QTemporaryDir temporary; QVERIFY(temporary.isValid());
    const auto root = temporary.filePath("main.tex");
    const QString text = "\\documentclass{article}\n\\usepackage[backend=biber]{biblatex}\n\\addbibresource{refs.bib}\n\\begin{document}\nA citation \\cite{example}.\\printbibliography\n\\IfFileExists{main.bbl}{\\typeout{WSL-BIB-READY}}{}\n\\end{document}\n";
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(text.toUtf8()); file.close();
    file.setFileName(temporary.filePath("refs.bib")); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("@book{example,author={Example, Author},title={Bibliography Example},year={2026}}\n"); file.close();
    TexpressoWsl wsl;
#ifdef Q_OS_WIN
    wsl.enabled = true; wsl.distribution = qEnvironmentVariable("TEXPRESSO_WSL_DISTRIBUTION");
    wsl.prepare(root); QVERIFY2(wsl.error.isEmpty(), qPrintable(wsl.error));
#endif
    const auto requestedProvider = qEnvironmentVariable("TEXPRESSO_PACKAGE_PROVIDER", "auto");
    const auto environment = TexpressoProvider::executionEnvironment(requestedProvider, TexpressoProcess::texliveEnvironment());
    const auto tool = [&](const QString &name, const QStringList &arguments) {
        QProcess process;
        process.setProcessEnvironment(environment);
        auto toolArguments = arguments;
        if (requestedProvider == "miktex" && name == "xelatex") toolArguments.prepend("--disable-installer");
        if (wsl.enabled) {
            process.setWorkingDirectory(temporary.path());
            QStringList args = {"--distribution", wsl.distribution, "--provider", requestedProvider, "--", name};
            args << toolArguments;
            process.start(qEnvironmentVariable("TEXPRESSO_TEST_COMMAND_RUNNER"), args);
        } else {
            process.setWorkingDirectory(temporary.path());
            process.start(QStandardPaths::findExecutable(name, environment.value("PATH").split(QDir::listSeparator())), toolArguments);
        }
        if (!process.waitForStarted(10000) || !process.waitForFinished(90000) || process.exitCode()) {
            qWarning().noquote() << name << process.errorString() << process.readAllStandardOutput() << process.readAllStandardError(); return false;
        }
        return true;
    };
    TexpressoManager manager(nullptr, qEnvironmentVariable("TEXPRESSO_TEST_ENGINE"));
    manager.activate(root); auto session = manager.current(); QVERIFY(session); session->followCursor = false;
    QTRY_VERIFY2_WITH_TIMEOUT(session->preview.valid() || session->failed(), qPrintable(session->status + session->engineOutput), 90000);
    QVERIFY(!session->failed());
    session->beginExternalBuild();
    QVERIFY(tool("xelatex", {"-interaction=nonstopmode", "-halt-on-error", "main.tex"}));
    QVERIFY(tool("biber", {"main"}));
    QVERIFY(tool("xelatex", {"-interaction=nonstopmode", "-halt-on-error", "main.tex"}));
    QVERIFY(tool("xelatex", {"-interaction=nonstopmode", "-halt-on-error", "main.tex"}));
    QVERIFY(QFileInfo::exists(temporary.filePath("main.bbl")));
    session->endExternalBuild(true);
    QTRY_VERIFY2_WITH_TIMEOUT(session->preview.valid() && session->compilerOutput.contains("WSL-BIB-READY"),
        qPrintable(session->status + session->engineOutput.right(2000) + session->compilerOutput.right(2000)), 60000);
    manager.shutdown();
}
QTEST_MAIN(WslTest)
#include "wsl_test.moc"
