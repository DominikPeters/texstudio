#ifndef TEXPRESSOWIDGET_H
#define TEXPRESSOWIDGET_H
#include "texpressomanager.h"
#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QToolButton>
#include <QComboBox>
#include <QSpinBox>
#include <QTimer>
#include <QPointer>

class TexpressoCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit TexpressoCanvas(QWidget *parent = nullptr) : QWidget(parent) {}
    QImage pixels;
    QRectF pageBounds;
    double scale = 1;
    bool fresh = false;
signals:
    void sourceClicked(const QPointF &point);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
};

class TexpressoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TexpressoWidget(TexpressoManager *manager, QWidget *parent = nullptr);
    void refresh();
signals:
    void syncRequested();
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void layoutPage();
    TexpressoManager *manager;
    QPointer<TexpressoSession> shown;
    QMetaObject::Connection revealConnection;
    TexpressoCanvas *canvas;
    QScrollArea *area;
    QLabel *status, *total, *project;
    QToolButton *pause, *stop, *restart, *previous, *next;
    QComboBox *zoom;
    QSpinBox *page;
    QTimer widthTimer;
    bool updating = false;
};
#endif
