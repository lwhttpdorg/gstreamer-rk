/*
 * GStreamer
 * Copyright (C) 2024 Rouven Czerwinski <entwicklung@pengutronix.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifndef QT6SWITEM_H_
#define QT6SWITEM_H_

#include <gst/video/video.h>
#include <gst/gst.h>
#include <QtQuick>
#include <QPainter>
#include <QImage>

typedef struct _Qt6SWVideoItemPrivate Qt6SWVideoItemPrivate;

class Qt6SWVideoItem;

class Qt6SWVideoItemInterface : public QObject
{
    Q_OBJECT
    QML_ELEMENT
public:
    Qt6SWVideoItemInterface (Qt6SWVideoItem *w) : qt_item (w), lock() {};

    void invalidateRef();

    void setSink (GstElement * sink);
    GstFlowReturn setBuffer (GstBuffer * buffer);
    gboolean setCaps (GstCaps *caps);
    Qt6SWVideoItem *videoItem () { return qt_item; };

    void setDAR(gint, gint);
    void getDAR(gint *, gint *);
    void setForceAspectRatio(bool);
    bool getForceAspectRatio();
private:
    Qt6SWVideoItem *qt_item;
    QMutex lock;
};

class Qt6SWVideoItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool itemInitialized
               READ itemInitialized
               NOTIFY itemInitializedChanged)
    Q_PROPERTY(bool forceAspectRatio
               READ getForceAspectRatio
               WRITE setForceAspectRatio
               NOTIFY forceAspectRatioChanged)

public:
    Qt6SWVideoItem(QQuickItem *parent = nullptr);
    ~Qt6SWVideoItem();

    void setDAR(gint, gint);
    void getDAR(gint *, gint *);
    void setForceAspectRatio(bool);
    bool getForceAspectRatio();

    void paint(QPainter *painter) override;
    bool itemInitialized();

    QSharedPointer<Qt6SWVideoItemInterface> getInterface() { return proxy; };
    /* private for C interface ... */
    Qt6SWVideoItemPrivate *priv;

Q_SIGNALS:
    void itemInitializedChanged();
    void forceAspectRatioChanged(bool);

private:
    QSharedPointer<Qt6SWVideoItemInterface> proxy;
};
#endif // QT6SWITEM_H_
