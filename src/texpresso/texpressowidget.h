#ifndef TEXPRESSOWIDGET_H
#define TEXPRESSOWIDGET_H
#include "texpressomanager.h"
#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QToolButton>
#include <QTimer>
#include <QPointer>
#include "ViewerControls.h"
#include "ViewerTools.h"

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
    void invalidateMagnifier() { magnifier->invalidate(); }
signals:
    void sourceClicked(const QPointF &point);
    void activated();
    void interactionFinished();
    void zoomRequested(int direction, const QPoint &anchor);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    void magnify(const QPoint &point);
    PDFDocumentConfig *config;
    QScrollArea *area;
    PDFMagnifier *magnifier;
    int tool = 1;
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
};

class TexpressoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TexpressoWidget(TexpressoManager *manager, PDFDocumentConfig *config,
        const ViewerControls::Icons &icons, QWidget *parent = nullptr);
    void refresh();
    const QHash<QString, QAction *> &viewerActions() const { return actions; }
    void configureAppearance(bool modern, int iconSize);
signals:
    void outputRequested();
    void syncRequested();
    void buildRequested();
    void followCursorChanged(bool enabled);
    void toolChanged(int tool);
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void layoutPage();
    void updateViewport(bool gesture = false);
    void connectCanvas(TexpressoCanvas *pageCanvas);
    void showPage(int page);
    void setZoom(qreal value, const QPoint &anchor = QPoint(-1, -1));
    TexpressoManager *manager;
    PDFDocumentConfig *config;
    QHash<QString, QAction *> actions;
    ViewerControls::PageControls pages;
    ViewerControls::ZoomControls zoomControls;
    QPointer<TexpressoSession> shown;
    QMetaObject::Connection revealConnection;
    TexpressoCanvas *canvas;
    QWidget *documentCanvas;
    QList<TexpressoCanvas *> extraCanvases;
    QList<QRect> pageRects;
    QHash<int, QRectF> knownBounds;
    int extentPages = 1, viewportPage = 0;
    bool continuous = true;
    QScrollArea *area;
    QToolBar *toolbar;
    QLabel *status, *project;
    QToolButton *pause, *stop, *restart;
    QTimer widthTimer;
    QTimer refreshTimer;
    QPointer<TexpressoSession> rasterSession;
    int rasterWidth = 0;
    bool shownInvert = false, shownGray = false;
    bool updating = false;
};
#endif
