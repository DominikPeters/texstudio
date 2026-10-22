/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2007-2010  Jonathan Kew

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

	For links to further information, or to contact the author,
	see <http://texworks.org/>.
*/

#pragma once
#include <QLabel>
#include <QPainterPath>
#include <functional>
#include "PDFDocument_config.h"

class QPainter;
QPixmap convertImage(const QPixmap &pixmap, bool invertColors, bool convertToGray);

class PDFDraggableTool : public QLabel
{
public:
	PDFDraggableTool(QWidget* parent): QLabel(parent) {}
	virtual void reshape() = 0;
	void drawGradient(QPainter& painter, const QRect& outline, QColor color, int padding, int magnifierShape);
};

class PDFMagnifier : public PDFDraggableTool
{
	Q_OBJECT

public:
	using Renderer = std::function<QPixmap(int, QObject *, qreal, const QRect &)>;
	PDFMagnifier(QWidget *parent, qreal inDpi, PDFDocumentConfig *config, Renderer renderer);
	QPainterPath highlight;
	QColor highlightColor;
	void invalidate() { imagePage = -1; }
	void setPage(int p, qreal scale, const QRect &visibleRect, const QRect &sampleRect = QRect());
	void reshape();

public slots:
	void setImage(const QPixmap &img, int pageNr);

protected:
	virtual void paintEvent(QPaintEvent *event);
	QPixmap &getConvertedImage();

private:
	PDFDocumentConfig *config;
	Renderer renderer;
	int page;
    qreal   overScale;
	qreal	scaleFactor;
	qreal	parentDpi;
	QPixmap	image;
	QPixmap	convertedImage;
	bool	convertedImageIsGrayscale;
	bool	convertedImageIsColorInverted;

	QPoint mouseTranslate;

	QPoint	imageLoc, mouseOffset;
	QSize	imageSize;
	qreal	imageDpi;
	int imagePage;
};

class PDFLaserPointer : public PDFDraggableTool {
	Q_OBJECT

public:
	PDFLaserPointer(QWidget *parent, PDFDocumentConfig *config);
	void reshape();
protected:
	virtual void paintEvent(QPaintEvent *event);
private:
	PDFDocumentConfig *config;
};
