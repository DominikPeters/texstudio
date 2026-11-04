#include "texpressowslsetup.h"
#include "texpressowsl.h"
#include "texpressoprocess.h"
#include <QWizard>
#include <QWizardPage>
#include <QComboBox>
#include <QCheckBox>
#include <QStandardPaths>
#include <QLabel>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStorageInfo>
#include <QFileInfo>
#include <QProcess>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <functional>

namespace {
class Page : public QWizardPage {
public:
    bool ready = true;
    std::function<void()> enter;
    bool isComplete() const override { return ready; }
    void initializePage() override { if (enter) enter(); }
    void complete(bool value) { ready = value; emit completeChanged(); }
};
QString windowsText(const QByteArray &bytes) {
    if (!bytes.contains('\0')) return QString::fromUtf8(bytes).trimmed();
    QString result;
    for (int i = 0; i + 1 < bytes.size(); i += 2) {
        const auto code = uchar(bytes[i]) | (quint16(uchar(bytes[i + 1])) << 8);
        if (code && code != 0xfeff) result += QChar(code);
    }
    return result.trimmed();
}
}
bool configureTexpressoWsl(QWidget *parent, QSettings *settings, const QString &root)
{
    QWizard wizard(parent); wizard.setWindowTitle(QObject::tr("Set up WSL live preview"));
    wizard.resize(650, 500);
    auto choice = new Page; choice->setTitle(QObject::tr("Choose your Linux environment"));
    auto choices = new QVBoxLayout(choice);
    auto introduction = new QLabel(QObject::tr("Live compilation runs inside WSL. Choose any installed distribution; TeX Live must be available inside that distribution."));
    introduction->setWordWrap(true); choices->addWidget(introduction);
    auto distribution = new QComboBox; distribution->addItem(QObject::tr("Default WSL distribution"), QString());
    distribution->setEditable(true); choices->addWidget(distribution);
    auto schemes = new QComboBox;
    schemes->addItem(QObject::tr("Full TeX Live — best package coverage, roughly 10 GB installed"), "full");
    schemes->addItem(QObject::tr("Smaller installation — fewer packages; add others as needed"), "small");
    choices->addWidget(schemes);
    auto builds = new QCheckBox(QObject::tr("Use this distribution's TeX Live for build commands and bibliography tools"));
    builds->setChecked(settings->value("texpresso/wslBuilds", QStandardPaths::findExecutable("kpsewhich.exe").isEmpty()).toBool());
    choices->addWidget(builds);
    auto warning = new QLabel(QObject::tr("If TeX Live is missing, the next page lets you install it. Installation downloads packages and takes additional disk space for temporary files. Full installation should have at least 12 GB available. Smaller installation size varies. Nothing is installed until you click Install TeX Live."));
    warning->setWordWrap(true); choices->addWidget(warning);
    auto guide = new QLabel(QObject::tr("<a href=\"https://learn.microsoft.com/windows/wsl/install\">Install WSL</a> · <a href=\"https://tug.org/texlive/quickinstall.html\">TeX Live installation guide</a>"));
    guide->setOpenExternalLinks(true); choices->addWidget(guide); choices->addStretch();
    auto review = new Page; review->setTitle(QObject::tr("Check or install TeX Live")); review->ready = false;
    auto reviewLayout = new QVBoxLayout(review);
    auto location = new QLabel; location->setWordWrap(true); reviewLayout->addWidget(location);
    auto output = new QPlainTextEdit; output->setReadOnly(true); output->setMaximumBlockCount(2000); reviewLayout->addWidget(output);
    auto install = new QPushButton(QObject::tr("Install TeX Live")); install->hide(); reviewLayout->addWidget(install);
    wizard.addPage(choice); wizard.addPage(review);
    QProcess list, process;
#ifdef Q_OS_WIN
    list.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
    const auto previous = settings->value("texpresso/wslDistribution").toString();
    QObject::connect(&list, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &wizard, [&](int code, QProcess::ExitStatus) {
        if (!code) for (const auto &line : windowsText(list.readAllStandardOutput()).split('\n')) {
            const auto value = line.trimmed(); if (!value.isEmpty()) distribution->addItem(value, value);
        }
        if (!previous.isEmpty()) { const int index = distribution->findData(previous); if (index >= 0) distribution->setCurrentIndex(index); else distribution->setEditText(previous); }
    });
    list.start("wsl.exe", {"--list", "--quiet"});
    TexpressoWsl wsl;
    QFutureWatcher<TexpressoWsl> prepare;
    bool installing = false, closing = false, missingTexLive = false;
    const auto check = [&] {
        missingTexLive = false;
        QStringList command = {"-c", TexpressoWsl::environmentScript(), "texstudio-wsl", wsl.toEngine(TexpressoProcess::bundledEngine()), "-capabilities"};
        process.start("wsl.exe", wsl.arguments("sh", command));
    };
    QObject::connect(&prepare, &QFutureWatcher<TexpressoWsl>::finished, &wizard, [&] {
        wsl = prepare.result();
        if (!wsl.error.isEmpty()) { output->appendPlainText(wsl.error); wizard.button(QWizard::BackButton)->setEnabled(true); return; }
        QString disk;
        if (wsl.freeBytes >= 0) disk = QObject::tr(" Space available inside Linux: %1 GB.").arg(wsl.freeBytes / 1e9, 0, 'f', 1);
#ifdef Q_OS_WIN
        QSettings registry("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Lxss", QSettings::NativeFormat);
        for (const auto &group : registry.childGroups()) {
            registry.beginGroup(group);
            if (registry.value("DistributionName").toString() == wsl.distribution) {
                QStorageInfo storage(registry.value("BasePath").toString());
                if (storage.isValid()) disk += QObject::tr(" Windows storage available: %1 GB.").arg(storage.bytesAvailable() / 1e9, 0, 'f', 1);
            }
            registry.endGroup();
        }
#endif
        location->setText(QObject::tr("Distribution: %1. A new installation goes into ~/.local/share/texlive/espresso inside this distribution.%2").arg(wsl.distribution, disk));
        check();
    });
    review->enter = [&] {
        wizard.button(QWizard::BackButton)->setEnabled(false);
        review->complete(false); install->hide(); output->clear(); output->appendPlainText(QObject::tr("Checking WSL and the bundled engine…"));
        TexpressoWsl selected; selected.enabled = true;
        selected.distribution = distribution->currentIndex() >= 0 && distribution->currentText() == distribution->itemText(distribution->currentIndex())
            ? distribution->currentData().toString() : distribution->currentText().trimmed();
        prepare.setFuture(QtConcurrent::run([selected, root]() mutable { selected.prepare(root); return selected; }));
    };
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &wizard, [&] { output->appendPlainText(QString::fromUtf8(process.readAllStandardOutput())); });
    QObject::connect(&process, &QProcess::readyReadStandardError, &wizard, [&] {
        const auto text = QString::fromUtf8(process.readAllStandardError());
        missingTexLive |= text.contains("TeX Live was not found"); output->appendPlainText(text);
    });
    QObject::connect(&process, &QProcess::errorOccurred, &wizard, [&](QProcess::ProcessError) { output->appendPlainText(process.errorString()); });
    QObject::connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &wizard, [&](int code, QProcess::ExitStatus status) {
        if (closing) return;
        if (installing) {
            installing = false;
            if (!code && status == QProcess::NormalExit) { output->appendPlainText(QObject::tr("Checking the installed environment…")); check(); }
            else { output->appendPlainText(QObject::tr("Installation did not finish. The log above explains the failure; partial installation files were retained.")); install->setEnabled(true); wizard.button(QWizard::BackButton)->setEnabled(true); }
            return;
        }
        const bool ready = code == 0 && status == QProcess::NormalExit;
        review->complete(ready); wizard.button(QWizard::BackButton)->setEnabled(true);
        if (ready) output->appendPlainText(QObject::tr("Ready. Click Finish to use this distribution."));
        install->setVisible(!ready && missingTexLive); install->setEnabled(true);
    });
    QObject::connect(install, &QPushButton::clicked, &wizard, [&] {
        install->setEnabled(false); wizard.button(QWizard::BackButton)->setEnabled(false); installing = true;
        const auto engine = wsl.toEngine(TexpressoProcess::bundledEngine());
        const auto bin = engine.left(engine.lastIndexOf('/') + 1);
        output->appendPlainText(QObject::tr("Installing TeX Live. This can take a long time; Cancel stops installation. Other TeX installations will be left alone."));
        process.start("wsl.exe", wsl.arguments(bin + "texpresso-wsl", {"/", "--", "/bin/sh", bin + "texstudio-install-texlive.sh", schemes->currentData().toString()}));
    });
    const bool accepted = wizard.exec() == QDialog::Accepted;
    closing = true;
    list.kill(); list.waitForFinished(1000);
    if (process.state() != QProcess::NotRunning) {
        process.closeWriteChannel();
        if (!process.waitForFinished(1500)) { process.kill(); process.waitForFinished(1000); }
    }
    if (accepted) {
        const auto selected = distribution->currentIndex() >= 0 && distribution->currentText() == distribution->itemText(distribution->currentIndex())
            ? distribution->currentData().toString() : distribution->currentText().trimmed();
        settings->setValue("texpresso/wslDistribution", selected);
        settings->setValue("texpresso/wslBuilds", builds->isChecked());
        TexpressoProcess::setWslDistribution(selected);
    }
    return accepted;
}
