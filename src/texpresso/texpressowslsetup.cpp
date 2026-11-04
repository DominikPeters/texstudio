#include "texpressowslsetup.h"
#include "texpressowsl.h"
#include "texpressoprocess.h"
#include "texpressoprovider.h"
#include <QDialogButtonBox>
#include <QJsonDocument>
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
struct ProviderOptions {
    QComboBox *provider;
    QCheckBox *install;
    ProviderOptions(QVBoxLayout *layout, QSettings *settings) {
        layout->addWidget(new QLabel(QObject::tr("TeX package provider")));
        provider = new QComboBox;
        provider->setObjectName("texpressoPackageProvider");
        provider->addItem(QObject::tr("Automatic"), "auto");
        provider->addItem(QObject::tr("TeX Live"), "texlive");
        provider->addItem(QObject::tr("MiKTeX"), "miktex");
        const int index = provider->findData(settings->value("texpresso/packageProvider", "auto"));
        provider->setCurrentIndex(index < 0 ? 0 : index);
        layout->addWidget(provider);
        install = new QCheckBox(QObject::tr("Allow MiKTeX to download missing packages during live compilation"));
        install->setObjectName("texpressoMiktexAutoInstall");
        install->setChecked(settings->value("texpresso/miktexAutoInstall", false).toBool());
        install->setEnabled(provider->currentData() != "texlive");
        QObject::connect(provider, &QComboBox::currentIndexChanged, install, [combo = provider, check = install] { check->setEnabled(combo->currentData() != "texlive"); });
        layout->addWidget(install);
        auto explanation = new QLabel(QObject::tr("Package downloads use disk space and can delay compilation. With this option off, live compilation never installs MiKTeX packages or opens installer dialogs."));
        explanation->setWordWrap(true); layout->addWidget(explanation);
    }
    void save(QSettings *settings) const {
        settings->setValue("texpresso/packageProvider", provider->currentData());
        settings->setValue("texpresso/miktexAutoInstall", install->isChecked());
        TexpressoProcess::setPackageProvider(provider->currentData().toString(), install->isChecked());
    }
};
class Page : public QWizardPage {
public:
    bool ready = true;
    std::function<void()> enter;
    bool isComplete() const override { return ready; }
    void initializePage() override { if (enter) enter(); }
    void complete(bool value) { ready = value; emit completeChanged(); }
};

}
bool configureTexpressoWsl(QWidget *parent, QSettings *settings, const QString &root)
{
    QWizard wizard(parent); wizard.setWindowTitle(QObject::tr("Set up WSL live preview"));
    wizard.resize(650, 500);
    auto choice = new Page; choice->setTitle(QObject::tr("Choose your Linux environment"));
    auto choices = new QVBoxLayout(choice);
    auto introduction = new QLabel(QObject::tr("Live compilation runs inside WSL. Choose any installed distribution; TeX Live or MiKTeX must be available inside that distribution. Windows-only installations cannot supply the Linux compiler."));
    introduction->setWordWrap(true); choices->addWidget(introduction);
    auto distribution = new QComboBox; distribution->addItem(QObject::tr("Default WSL distribution"), QString());
    distribution->setEditable(true); choices->addWidget(distribution);
    ProviderOptions provider(choices, settings);
    auto schemes = new QComboBox;
    schemes->addItem(QObject::tr("Full TeX Live — best package coverage, roughly 10 GB installed"), "full");
    schemes->addItem(QObject::tr("Smaller installation — fewer packages; add others as needed"), "small");
    choices->addWidget(schemes);
    auto builds = new QCheckBox(QObject::tr("Use this distribution for build commands and bibliography tools"));
    builds->setChecked(settings->value("texpresso/wslBuilds", (QStandardPaths::findExecutable("kpsewhich.exe").isEmpty() && QStandardPaths::findExecutable("findtexmf.exe").isEmpty())).toBool());
    choices->addWidget(builds);
    auto warning = new QLabel(QObject::tr("If TeX Live is missing, the next page lets you install it. Installation downloads packages and takes additional disk space for temporary files. Full installation should have at least 12 GB available. Smaller installation size varies. Nothing is installed until you click Install TeX Live."));
    warning->setWordWrap(true); choices->addWidget(warning);
    auto guide = new QLabel(QObject::tr("<a href=\"https://learn.microsoft.com/windows/wsl/install\">Install WSL</a> · <a href=\"https://tug.org/texlive/quickinstall.html\">TeX Live installation guide</a>"));
    guide->setOpenExternalLinks(true); choices->addWidget(guide); choices->addStretch();
    auto review = new Page; review->setTitle(QObject::tr("Check your TeX environment")); review->ready = false;
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
        if (!code) for (const auto &line : TexpressoWsl::windowsText(list.readAllStandardOutput()).split('\n')) {
            const auto value = line.trimmed(); if (!value.isEmpty()) distribution->addItem(value, value);
        }
        if (!previous.isEmpty()) { const int index = distribution->findData(previous); if (index >= 0) distribution->setCurrentIndex(index); else distribution->setEditText(previous); }
    });
    list.start("wsl.exe", {"--list", "--quiet"});
    TexpressoWsl wsl;
    QFutureWatcher<TexpressoWsl> prepare;
    bool installing = false, closing = false, missingTexLive = false;
    QByteArray capabilityOutput;
    const auto check = [&] {
        missingTexLive = false; capabilityOutput.clear();
        QStringList command = {"-c", TexpressoWsl::environmentScript(wsl.requestedProvider), "texstudio-wsl", wsl.toEngine(TexpressoProcess::bundledEngine()), "-capabilities"};
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
        selected.requestedProvider = provider.provider->currentData().toString();
        selected.distribution = distribution->currentIndex() >= 0 && distribution->currentText() == distribution->itemText(distribution->currentIndex())
            ? distribution->currentData().toString() : distribution->currentText().trimmed();
        prepare.setFuture(QtConcurrent::run([selected, root]() mutable { selected.prepare(root); return selected; }));
    };
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &wizard, [&] {
        const auto bytes = process.readAllStandardOutput();
        if (!installing) capabilityOutput += bytes;
        output->appendPlainText(QString::fromUtf8(bytes));
    });
    QObject::connect(&process, &QProcess::readyReadStandardError, &wizard, [&] {
        const auto text = QString::fromUtf8(process.readAllStandardError());
        missingTexLive |= text.contains("TeX package provider was not found"); output->appendPlainText(text);
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
        const auto capabilities = QJsonDocument::fromJson(capabilityOutput).object();
        const bool supported = !TexpressoProvider::engineArguments(wsl.provider, provider.install->isChecked(), capabilities).isEmpty();
        const bool ready = code == 0 && status == QProcess::NormalExit && supported && capabilities.value("previewProtocol").toInt() == 1;
        if (!ready && code == 0) output->appendPlainText(QObject::tr("The bundled engine does not support this provider or installation option. Select another provider or update the application."));
        review->complete(ready); wizard.button(QWizard::BackButton)->setEnabled(true);
        if (ready) output->appendPlainText(QObject::tr("Ready. Click Finish to use this distribution."));
        install->setVisible(!ready && missingTexLive && wsl.requestedProvider != "miktex"); install->setEnabled(true);
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
        provider.save(settings);
        settings->setValue("texpresso/wslDistribution", selected);
        settings->setValue("texpresso/wslBuilds", builds->isChecked());
        TexpressoProcess::setWslDistribution(selected);
    }
    return accepted;
}

bool configureTexpressoEnvironment(QWidget *parent, QSettings *settings, const QString &root)
{
#ifdef Q_OS_WIN
    return configureTexpressoWsl(parent, settings, root);
#else
    Q_UNUSED(root)
    QDialog dialog(parent); dialog.setWindowTitle(QObject::tr("TeX environment"));
    auto layout = new QVBoxLayout(&dialog);
    ProviderOptions options(layout, settings);
    auto explanation = new QLabel(QObject::tr("Automatic uses the installed provider. When separate TeX Live and MiKTeX installations are found, TeX Live is preferred. This choice affects live preview; normal build recipes retain their configured commands."));
    explanation->setWordWrap(true); layout->addWidget(explanation);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    options.save(settings); return true;
#endif
}
