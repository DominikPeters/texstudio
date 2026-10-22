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

#include "ViewerTools.h"
#include <QPainter>
#include <QPaintEvent>
#include <QRegion>
#include <utility>

QPixmap convertImage(const QPixmap &pixmap, bool invertColors, bool convertToGray)
{
	if (pixmap.isNull()) return pixmap;

	QImage img = pixmap.toImage();
	if (invertColors)
		img.invertPixels();
	if (convertToGray) {
		QImage retImg(img.width(), img.height(), QImage::Format_Indexed8);
		QVector<QRgb> table(256);
		for ( int i = 0; i < 256; ++i) {
			table[i] = qRgb(i, i, i);
		}
		retImg.setColorTable(table);
		for (int i = 0; i < img.width(); i++) {
			for (int j = 0; j < img.height(); j++) {
				QRgb value = img.pixel(i, j);
                retImg.setPixel(i, j, static_cast<uint>(qGray(value)));
			}
		}
		return QPixmap::fromImage(retImg);
	}
	return QPixmap::fromImage(img);
}

//================== PDFMagnifier ========================

const int kMagFactor = 2;

PDFMagnifier::PDFMagnifier(QWidget *parent, qreal inDpi, PDFDocumentConfig *config, Renderer renderer)
    : PDFDraggableTool(parent)
    , config(config), renderer(std::move(renderer))
	, page(-1)
	, overScale(1)
	, scaleFactor(kMagFactor)
	, parentDpi(inDpi)
	, convertedImageIsGrayscale(false)
	, convertedImageIsColorInverted(false)
	, imageDpi(0)
	, imagePage(-1)
{
	setAttribute(Qt::WA_TransparentForMouseEvents);
}

void PDFMagnifier::setPage(int pageNr, qreal scale, const QRect &visibleRect, const QRect &sampleRect)
{
	page = pageNr;

	overScale = this->devicePixelRatio();

	scaleFactor = scale * kMagFactor;
	if (page < 0) {
		imagePage = -1;
		image = QPixmap();
		convertedImage = QPixmap();
		convertedImageIsGrayscale = false;
		convertedImageIsColorInverted = false;
	} else {
		QWidget *parent = parentWidget();
        if (parent != nullptr) {
			QWidget *viewport = parent->parentWidget();
            if (viewport != nullptr && viewport->parentWidget() != nullptr) {
				qreal dpi = parentDpi * scaleFactor;
				QPoint tl = parent->mapFromParent(viewport->rect().topLeft());
				QPoint br = parent->mapFromParent(viewport->rect().bottomRight());
                if (!sampleRect.isNull()) { tl = sampleRect.topLeft(); br = sampleRect.bottomRight(); }
				tl -= visibleRect.topLeft();
				br -= visibleRect.topLeft();
				if (tl.x() < 0) tl.setX(0);
				if (tl.y() < 0) tl.setY(0);
				if (br.x() > visibleRect.width()) br.setX(visibleRect.width());
				if (br.y() > visibleRect.height()) br.setY(visibleRect.height());
				QSize  size = QSize(br.x() - tl.x(), br.y() - tl.y()) * kMagFactor;
				QPoint loc = tl * kMagFactor;
                if (page != imagePage || qAbs(dpi/imageDpi-1.0)>0.001 || loc != imageLoc || size != imageSize) {
					//don't cache in rendermanager in order to reduce memory consumption
					image = renderer(pageNr, this, dpi * overScale, QRect(loc * overScale, size * overScale));
					convertedImage = QPixmap();
					convertedImageIsGrayscale = convertedImageIsColorInverted = false;
				}
				imagePage = page;
				imageDpi = dpi;
				imageLoc = loc;
				imageSize = size;

				mouseTranslate = rect().center() - imageLoc  - (visibleRect.topLeft() /*- offset*/) * kMagFactor;

			}
		}
    }
	update();
}

void PDFMagnifier::reshape()
{
	Q_ASSERT(config);
	if (!config) return;

	switch (config->magnifierShape) {
	case 1: { //circular
		int side = qMin(width(), height());
		QRegion maskedRegion(width() / 2 - side / 2, height() / 2 - side / 2, side, side, QRegion::Ellipse);
		setMask(maskedRegion);
		break;
	}
	default: //rectangular
		setMask(QRect(0, 0, width(), height()));
	}
}

void PDFMagnifier::setImage(const QPixmap &img, int pageNr)
{
	if (pageNr == page) {
		image = img;
		convertedImage = QPixmap();
		convertedImageIsGrayscale = false;
		convertedImageIsColorInverted = false;
	}
	update();
}

void PDFDraggableTool::drawGradient(QPainter& painter, const QRect& outline, QColor color, int padding, int magnifierShape)
{
	painter.save();

	if(magnifierShape == PDFDocumentConfig::Circle) {
		QRadialGradient gradient(outline.center(), outline.width() / 2.0 , outline.center());
		color.setAlpha(0);
		gradient.setColorAt(1.0, color);
		color.setAlpha(64);
		gradient.setColorAt(1.0 - padding * 2.0 / (outline.width()), color);
		painter.fillRect(outline, gradient);
	} else { // rectangle
		color.setAlpha(64);

		// A rectangular shadow is constructed by drawing
		// four quarter circles at the corners, and
		// linking them with four rectangles.

		// limit draw region at the four edges to construct
		// quarter circles from the drawn full circles
		QPainterPath edgePath;
		edgePath.addRect(0, 0, padding, padding);
		edgePath.addRect(0, outline.height() - padding, padding, padding);
		edgePath.addRect(outline.width() - padding, 0, padding, padding);
		edgePath.addRect(outline.width() - padding, outline.height() - padding, padding, padding);
		painter.setClipPath(edgePath);

		// draw the four circles
		QRadialGradient circle1(padding,padding,padding,padding,padding);
		circle1.setColorAt(0, color);
		circle1.setColorAt(1, QColor("transparent"));
		painter.fillRect(outline, circle1);

		QRadialGradient circle2(padding,outline.height() - padding,padding,padding,outline.height() - padding);
		circle2.setColorAt(0, color);
		circle2.setColorAt(1, QColor("transparent"));
		painter.fillRect(outline, circle2);

		QRadialGradient circle3(outline.width() - padding,padding,padding,outline.width() - padding,padding);
		circle3.setColorAt(0, color);
		circle3.setColorAt(1, QColor("transparent"));
		painter.fillRect(outline, circle3);

		QRadialGradient circle4(outline.width() - padding,outline.height() - padding,padding,outline.width() - padding,outline.height() - padding);
		circle4.setColorAt(0, color);
		circle4.setColorAt(1, QColor("transparent"));
		painter.fillRect(outline, circle4);


		// now the four rectangular sides of the shadow are drawn:
		QLinearGradient sideGradient;
		sideGradient.setColorAt(0, color);
		sideGradient.setColorAt(1, QColor("transparent"));

		// draw the top rectangle
		edgePath = QPainterPath();
		edgePath.addRect(padding, 0, outline.width() - 2*padding, padding);
		sideGradient.setStart(0, padding);
		sideGradient.setFinalStop(0, 0);
		painter.setClipPath(edgePath);
		painter.fillRect(outline, sideGradient);

		// draw the bottom rectangle
		edgePath = QPainterPath();
		edgePath.addRect(padding, outline.height() - padding, outline.width() - 2*padding, padding);
		sideGradient.setStart(0, outline.height() - padding);
		sideGradient.setFinalStop(0, outline.height());
		painter.setClipPath(edgePath);
		painter.fillRect(outline, sideGradient);

		// draw the left rectangle
		edgePath = QPainterPath();
		edgePath.addRect(0, padding, padding, outline.height() - 2*padding);
		sideGradient.setStart(padding, 0);
		sideGradient.setFinalStop(0, 0);
		painter.setClipPath(edgePath);
		painter.fillRect(outline, sideGradient);

		// draw the right rectangle
		edgePath = QPainterPath();
		edgePath.addRect(outline.width() - padding, padding, padding, outline.height() - 2*padding);
		sideGradient.setStart(outline.width()-padding, 0);
		sideGradient.setFinalStop(outline.width(), 0);
		painter.setClipPath(edgePath);
		painter.fillRect(outline, sideGradient);

		// fill background
		edgePath = QPainterPath();
		edgePath.addRect(padding, padding, outline.width() - 2*padding, outline.height() - 2*padding);
		painter.setClipPath(edgePath);
		painter.fillRect(outline, color);
	}

	painter.restore();
}

void PDFMagnifier::paintEvent(QPaintEvent *event)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	drawFrame(&painter);

	QRect tmpRect(event->rect().x()*overScale, event->rect().y()*overScale, event->rect().width()*overScale, event->rect().height()*overScale);
	const int side = qMin(width(), height()) ;

	// Define a path that specifies the border of the magnifier.
	// This path is also later reused to draw the actual border.
	QPainterPath borderPath;
	if(config->magnifierShadow) {
		if(config->magnifierShape == PDFDocumentConfig::Circle) {
			// circular magnifier with transparent shadow
			const int shadowWidth=13;
			const int magnifierWidth = side - 2*shadowWidth + 2;

			// draw transparent shadow
			QRect outline(width() / 2 - side / 2 + 1, height() / 2 - side / 2 + 1, side - 2, side - 2);
			if (config->invertColors){
				drawGradient(painter, outline, QColor(Qt::white), shadowWidth, config->magnifierShape);
			}else{
				drawGradient(painter, outline, QColor(Qt::black), shadowWidth, config->magnifierShape);
			}

			borderPath.addRoundedRect(
				width()/2 - side/2 + shadowWidth - 2,
				height()/2 - side/2 + shadowWidth - 5, // magnifier moved upwards for 3D effect
				magnifierWidth,
				magnifierWidth,
				magnifierWidth/2,
				magnifierWidth/2
			);
		} else {
			// rectangular magnifier with transparent shadow
			const int shadowWidth = 7;

			// draw transparent shadow
			QRect outline(0, 0, width(), height());
			if (config->invertColors){
				drawGradient(painter, outline, QColor(Qt::white), shadowWidth, config->magnifierShape);
			}else{
				drawGradient(painter, outline, QColor(Qt::black), shadowWidth, config->magnifierShape);
			}

			borderPath.addRect(5, 5, width() - shadowWidth - 3, height() - shadowWidth - 6);

		}
	} else {
		if(config->magnifierShape == PDFDocumentConfig::Circle) {
			// circular magnifier without shadow
			const int magnifierWidth = side - 4;

			borderPath.addRoundedRect(
				width()/2 - side/2 + 2,
				height()/2 - side/2 + 2,
				magnifierWidth,
				magnifierWidth,
				magnifierWidth/2,
				magnifierWidth/2
			);
		} else {
			// rectangular magnifier without shadow
			borderPath.addRect(1, 1, width() - 2, height() - 2);
		}
	}

	// draw contents inside the magnifier
	painter.setClipPath(borderPath);
	painter.drawPixmap(event->rect(), getConvertedImage(), tmpRect.translated(kMagFactor * overScale * pos() + mouseTranslate * overScale));

	// The renderer supplies its source highlight in page coordinates.
    if (!highlight.isEmpty()) {
        painter.save();
        painter.translate(-kMagFactor * pos() - mouseTranslate - imageLoc);
        painter.scale(imageDpi / 72.0, imageDpi / 72.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(highlightColor);
        painter.setCompositionMode(QPainter::CompositionMode_Multiply);
        painter.drawPath(highlight);
        painter.restore();
    }

	// draw a border around the magnifier
	if (config->magnifierBorder) {
		if(config->magnifierShadow) {
			if (config->invertColors){
				painter.setPen(QPen(QPalette().light().color(), 2)); // white outline
			} else {
				painter.setPen(QPen(QPalette().shadow().color(), 2)); // black outline
			}
		} else {
			painter.setPen(QPen(QPalette().mid().color(), 2)); // gray outline
		}
		painter.drawPath(borderPath);
	}

}

/* lazy evaluation of image convertion */
QPixmap &PDFMagnifier::getConvertedImage()
{
	if (!config->invertColors && !config->grayscale) {  // no processing needed
		return image;
	}
	if (config->invertColors == convertedImageIsColorInverted && config->grayscale == convertedImageIsGrayscale) {  // conversion already done
		return convertedImage;
	}
	convertedImage = convertImage(image, config->invertColors, config->grayscale);
    convertedImageIsColorInverted = config->invertColors;
    convertedImageIsGrayscale = config->grayscale;
	return convertedImage;
}


PDFLaserPointer::PDFLaserPointer(QWidget *parent, PDFDocumentConfig *config)
    : PDFDraggableTool(parent), config(config)
{
}

void PDFLaserPointer::reshape()
{
	int side = qMin(width(), height());
	QRegion maskedRegion(width() / 2 - side / 2, height() / 2 - side / 2, side, side, QRegion::Ellipse);
	setMask(maskedRegion);
}

void PDFLaserPointer::paintEvent(QPaintEvent *event)
{
	QPainter painter(this);
	drawFrame(&painter);
	qreal overScale = this->devicePixelRatio();
	QRect tmpRect(event->rect().x()*overScale, event->rect().y()*overScale, event->rect().width()*overScale, event->rect().height()*overScale);
	int side = qMin(width(), height()) ;
	QRect outline(width() / 2 - side / 2 + 1, height() / 2 - side / 2 + 1, side - 2, side - 2);

	drawGradient(painter, outline, QColor(config ? config->laserPointerColor : "#ff0000"), 5, PDFDocumentConfig::Circle);
}
