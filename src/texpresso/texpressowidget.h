#ifndef TEXPRESSOWIDGET_H
#define TEXPRESSOWIDGET_H
#include "texpressomanager.h"
#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QToolButton>
#include <QStatusBar>
#include <QTimer>
#include <QPointer>
#include "ViewerControls.h"
#include "ViewerTools.h"
class QPushButton;

class TexpressoCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit TexpressoCanvas(PDFDocumentConfig *config, QScrollArea *area);
    QImage pixels;
    QRectF pageBounds;
    double scale = 1;
    bool fresh = false;
    bool interactionActive = false;
    void setTool(int tool);
    void resetTools();
    void setPreview(TexpressoPreview *preview);
    void setAnnotations(int page, qint64 revision, qint64 sequence, const QJsonObject &annotations);
    void setTikzFigures(const QJsonArray &figures, bool available, const QIcon &icon = QIcon());
    void invalidateMagnifier() { magnifier->invalidate(); }
signals:
    void sourceClicked(const QPointF &point);
    void activated();
    void interactionFinished();
    void zoomRequested(int direction, const QPoint &anchor);
    void scaleRequested(qreal factor, const QPoint &anchor);
    void linkActivated(int page, int index, qint64 revision, qint64 sequence);
    void tikzActivated(int page, int index, qint64 revision, qint64 sequence);
protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
private:
    friend class PaginationTest;
    friend class TikzTest;
    int linkAt(const QPoint &point) const;
    void updateLinkCursor(const QPoint &point, Qt::KeyboardModifiers modifiers);
    void magnify(const QPoint &point);
    int figureAt(const QPoint &point) const;
    QRectF figureRect(int index) const;
    QRect figureVisibleArea() const;
    bool figureHoverContains(const QPoint &globalPoint) const;
    void showFigurePopup();
    void hideFigurePopup();
    void activateFigure(int index);
    QJsonArray tikzFigures;
    QWidget *figurePopup;
    QPushButton *editFigure;
    QIcon tikzIcon;
    QTimer figureTimer, figureLeaveTimer;
    int hoveredFigure = -1;
    bool tikzAvailable = false;
    PDFDocumentConfig *config;
    QScrollArea *area;
    PDFMagnifier *magnifier;
    int tool = 1;
    qreal pinchTotal = 1;
    bool dragging = false;
    QPoint dragPosition;
    Qt::KeyboardModifiers pressedModifiers;
    QPointer<TexpressoPreview> preview;
    QMetaObject::Connection regionConnection;
    QRect lensRegion;
    qreal lensResolution = 0;
    qint64 lensToken = 0;
    QPixmap convertedPixels;
    qint64 convertedKey = 0;
    bool convertedInvert = false, convertedGray = false;
    QJsonObject annotations;
    int annotationPage = -1, pressedLink = -1;
    qint64 annotationRevision = -1, annotationSequence = -1;
    QPoint linkPressPosition;
    bool linkDragged = false, linkGesture = false;
};

class TexpressoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TexpressoWidget(TexpressoManager *manager, PDFDocumentConfig *config,
        const ViewerControls::Icons &icons, QWidget *parent = nullptr);
    void refresh();
    void setPreviewVisible(bool visible);
    const QHash<QString, QAction *> &viewerActions() const { return actions; }
    void configureAppearance(bool modern, int iconSize);
    void setTikzEditor(const QString &executable);
signals:
    void environmentSetupRequested();
    void outputRequested();
    void syncRequested();
    void buildRequested();
    void followCursorChanged(bool enabled);
    void toolChanged(int tool);
    void tikzRequested(const QString &file, int oneBasedLine, int endLine);
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    friend class SourceViewportTest;
    friend class PaginationTest;
    void layoutPage();
    void updateViewport(bool gesture = false);
    void connectCanvas(TexpressoCanvas *pageCanvas);
    void updateCanvas(TexpressoCanvas *pageCanvas, int page);
    void showPage(int page);
    void revealPoint(const QPointF &point, bool source);
    void setZoom(qreal value, const QPoint &anchor = QPoint(-1, -1));
    enum Severity { StatusPlain, StatusWarning, StatusError };
    void setStatus(const QString &text, Severity severity = StatusPlain, const QString &toolTip = QString());
    void updateStateIcons();
    TexpressoManager *manager;
    PDFDocumentConfig *config;
    ViewerControls::Icons icons;
    QHash<QString, QAction *> actions;
    QList<QAction *> contextActions;
    ViewerControls::PageControls pages;
    ViewerControls::ZoomControls zoomControls;
    QPointer<TexpressoSession> shown;
    QMetaObject::Connection revealConnection;
    QMetaObject::Connection externalLinkConnection;
    TexpressoCanvas *canvas;
    QWidget *documentCanvas;
    QList<TexpressoCanvas *> extraCanvases;
    QList<QRect> pageRects;
    QHash<int, QRectF> knownBounds;
    int extentPages = 1, viewportPage = 0;
    bool continuous = true;
    bool previewEnabled = true;
    QScrollArea *area;
    QToolBar *toolbar;
    QStatusBar *statusBar;
    QLabel *status, *statusIcon;
    Severity statusSeverity = StatusPlain;
    int iconState = -1;
    QTimer widthTimer;
    QTimer refreshTimer;
    QPointer<TexpressoSession> rasterSession;
    int rasterWidth = 0;
    bool shownInvert = false, shownGray = false;
    bool updating = false;
    QPoint zoomScrollPosition = QPoint(-1, -1);
    QPointF zoomScrollRemainder;
    double lastZoomMode = 0;
    QRectF lastTextRange;
    QString tikzEditor;
    QIcon tikzEditorIcon;
};
#endif
