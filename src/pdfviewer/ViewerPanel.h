#pragma once
#include <QWidget>
#include <QHash>
#include <QPointer>

class QSettings;
class QStackedWidget;
class QToolButton;

// One embedded preview area. Renderers keep their own tools and view state.
class ViewerPanel : public QWidget
{
    Q_OBJECT
public:
    enum Mode { Live, Pdf };
    Q_ENUM(Mode)
    explicit ViewerPanel(QSettings *settings, QWidget *parent = nullptr);
    ~ViewerPanel() override;
    void setLiveViewer(QWidget *viewer);
    void setPdfSupported(bool supported);
    void setProject(const QString &project);
    QString project() const { return currentProject; }
    Mode mode() const { return currentMode; }
    void setMode(Mode mode);
    void addPdf(const QString &project, QWidget *viewer);
    QWidget *pdf(const QString &project) const;
    QWidget *currentViewer() const;
    bool liveVisible() const;
signals:
    void modeChanged(ViewerPanel::Mode mode);
    void currentViewerChanged(QWidget *viewer);
    void liveVisibilityChanged(bool visible);
    void pdfRequested(const QString &project);
    void buildRequested();
protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    void refresh(bool requestPdf = false);
    void updateLiveVisibility();
    QString settingsKey() const;
    QSettings *settings;
    QStackedWidget *stack;
    QToolButton *liveButton, *pdfButton;
    QWidget *emptyPdf;
    QPointer<QWidget> live;
    QHash<QString, QPointer<QWidget>> pdfs;
    QString currentProject;
    Mode currentMode = Live;
    bool pdfSupported = true, lastLiveVisible = false;
};
