#include "qt6switem.h"
#include <gst/video/video-info.h>

#define DEFAULT_FORCE_ASPECT_RATIO  TRUE
#define DEFAULT_PAR_N               0
#define DEFAULT_PAR_D               1

enum
{
  PROP_0,
  PROP_FORCE_ASPECT_RATIO,
};

struct _Qt6SWVideoItemPrivate
{
  GMutex lock;

  gboolean force_aspect_ratio;
  gint par_n, par_d;

  GWeakRef sink;

  GstBuffer *buffer;

  gint display_width;
  gint display_height;

  GstCaps *new_caps;
  GstCaps *caps;
  GstVideoInfo new_v_info;
  GstVideoInfo v_info;
};

Qt6SWVideoItem::Qt6SWVideoItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
  this->setFlag (QQuickItem::ItemHasContents, true);

  this->priv = g_new0 (Qt6SWVideoItemPrivate, 1);

  this->proxy = QSharedPointer<Qt6SWVideoItemInterface>(new Qt6SWVideoItemInterface(this));

  this->priv->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  this->priv->par_n = DEFAULT_PAR_N;
  this->priv->par_d = DEFAULT_PAR_D;

  emit itemInitializedChanged();
}

bool
Qt6SWVideoItem::itemInitialized()
{
  return true;
}

void
Qt6SWVideoItem::setDAR(gint num, gint den)
{
  this->priv->par_n = num;
  this->priv->par_d = den;
}

void
Qt6SWVideoItem::getDAR(gint * num, gint * den)
{
  if (num)
    *num = this->priv->par_n;
  if (den)
    *den = this->priv->par_d;
}

void Qt6SWVideoItem::paint(QPainter *painter)
{
  GstMapInfo map_info;

  GstVideoInfo v_info = priv->v_info;
  GstVideoRectangle src, dst, result;

  g_mutex_lock (&this->priv->lock);

  QRectF source(0, 0, v_info.width, v_info.height);

  if (!priv->buffer) {
    g_mutex_unlock (&this->priv->lock);
    return;
  }

  if (!gst_buffer_map (priv->buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (priv->buffer, "Couldn't map incoming buffer");
    g_mutex_unlock (&this->priv->lock);
    return;
  }

  if (this->priv->force_aspect_ratio) {
    src.w = this->priv->display_width;
    src.h = this->priv->display_height;

    dst.x = boundingRect().x();
    dst.y = boundingRect().y();
    dst.w = boundingRect().width();
    dst.h = boundingRect().height();

    gst_video_sink_center_rect (src, dst, &result, TRUE);
  } else {
    result.x = boundingRect().x();
    result.y = boundingRect().y();
    result.w = boundingRect().width();
    result.h = boundingRect().height();
  }

  QRectF target(result.x, result.y, result.w, result.h);
  QImage image = QImage(map_info.data, v_info.width, v_info.height, v_info.stride[0], QImage::Format_RGB32);

  GST_TRACE ("Painting from %d:%d to %d:%d with %d", (int)source.width(),
            (int)source.height(), (int)target.width(), (int)target.height(), v_info.finfo->format );
  painter->drawImage(target, image, source);

  gst_buffer_unmap (priv->buffer, &map_info);

  g_mutex_unlock (&this->priv->lock);
}

void
Qt6SWVideoItem::setForceAspectRatio(bool force_aspect_ratio)
{
  this->priv->force_aspect_ratio = !!force_aspect_ratio;

  emit forceAspectRatioChanged(force_aspect_ratio);
}

bool
Qt6SWVideoItem::getForceAspectRatio()
{
  return this->priv->force_aspect_ratio;
}

Qt6SWVideoItem::~Qt6SWVideoItem()
{
  GST_INFO ("%p Destroying QtSWVideoItem and invalidating the proxy %p", this, proxy.data());
  proxy->invalidateRef();
  proxy.clear();

  if (priv->buffer) {
    gst_buffer_unref(priv->buffer);
  }

  g_free (this->priv);
  this->priv = NULL;
}

void
Qt6SWVideoItemInterface::invalidateRef()
{
  QMutexLocker locker(&lock);
  qt_item = NULL;
}

void
Qt6SWVideoItemInterface::setSink (GstElement * sink)
{
  QMutexLocker locker(&lock);
  if (qt_item == NULL)
    return;

  g_mutex_lock (&qt_item->priv->lock);
  g_weak_ref_set (&qt_item->priv->sink, sink);
  g_mutex_unlock (&qt_item->priv->lock);
}

static gboolean
_calculate_par (Qt6SWVideoItem * widget, GstVideoInfo * info)
{
  gboolean ok;
  gint width, height;
  gint par_n, par_d;
  gint display_par_n, display_par_d;
  guint display_ratio_num, display_ratio_den;

  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);

  par_n = GST_VIDEO_INFO_PAR_N (info);
  par_d = GST_VIDEO_INFO_PAR_D (info);

  if (!par_n)
    par_n = 1;

  /* get display's PAR */
  if (widget->priv->par_n != 0 && widget->priv->par_d != 0) {
    display_par_n = widget->priv->par_n;
    display_par_d = widget->priv->par_d;
  } else {
    display_par_n = 1;
    display_par_d = 1;
  }

  ok = gst_video_calculate_display_ratio (&display_ratio_num,
      &display_ratio_den, width, height, par_n, par_d, display_par_n,
      display_par_d);

  if (!ok)
    return FALSE;

  widget->setImplicitWidth (width);
  widget->setImplicitHeight (height);

  GST_LOG ("%p PAR: %u/%u DAR:%u/%u", widget, par_n, par_d, display_par_n,
      display_par_d);

  if (height % display_ratio_den == 0) {
    GST_DEBUG ("%p keeping video height", widget);
    widget->priv->display_width = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    widget->priv->display_height = height;
  } else if (width % display_ratio_num == 0) {
    GST_DEBUG ("%p keeping video width", widget);
    widget->priv->display_width = width;
    widget->priv->display_height = (guint)
        gst_util_uint64_scale_int (width, display_ratio_den, display_ratio_num);
  } else {
    GST_DEBUG ("%p approximating while keeping video height", widget);
    widget->priv->display_width = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    widget->priv->display_height = height;
  }
  GST_DEBUG ("%p scaling to %dx%d", widget, widget->priv->display_width,
      widget->priv->display_height);

  return TRUE;
}

GstFlowReturn
Qt6SWVideoItemInterface::setBuffer (GstBuffer * buffer)
{
  QMutexLocker locker(&lock);
  if (qt_item == NULL) {
    GST_WARNING ("%p actual item is NULL. setBuffer call ignored", this);
    return GST_FLOW_ERROR;
  }

  if (!qt_item->priv->caps && !qt_item->priv->new_caps) {
    GST_WARNING ("%p Got buffer on unnegotiated QtSWVideoItem. Dropping", this);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&qt_item->priv->lock);

  if (qt_item->priv->new_caps) {
    GST_DEBUG ("%p caps change from %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT,
        this, qt_item->priv->caps, qt_item->priv->new_caps);
    gst_caps_take (&qt_item->priv->caps, qt_item->priv->new_caps);
    qt_item->priv->new_caps = NULL;
    qt_item->priv->v_info = qt_item->priv->new_v_info;

    if (!_calculate_par (qt_item, &qt_item->priv->v_info)) {
      g_mutex_unlock (&qt_item->priv->lock);
      return GST_FLOW_ERROR;
    }
  }

  gst_buffer_replace (&qt_item->priv->buffer, buffer);

  g_mutex_unlock (&qt_item->priv->lock);

  QMetaObject::invokeMethod(qt_item, "update", Qt::QueuedConnection);

  return GST_FLOW_OK;
}

gboolean
Qt6SWVideoItemInterface::setCaps (GstCaps * caps)
{
  QMutexLocker locker(&lock);
  GstVideoInfo v_info;

  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  if (qt_item == NULL)
    return FALSE;

  if (qt_item->priv->caps && gst_caps_is_equal_fixed (qt_item->priv->caps, caps))
    return TRUE;

  if (!gst_video_info_from_caps (&v_info, caps))
    return FALSE;

  g_mutex_lock (&qt_item->priv->lock);

  GST_DEBUG ("%p set caps %" GST_PTR_FORMAT, qt_item, caps);

  gst_caps_replace (&qt_item->priv->new_caps, caps);

  qt_item->priv->new_v_info = v_info;

  g_mutex_unlock (&qt_item->priv->lock);

  return TRUE;
}

void
Qt6SWVideoItemInterface::setForceAspectRatio(bool force_aspect_ratio)
{
  QMutexLocker locker(&lock);
  if (!qt_item)
    return;
  qt_item->setForceAspectRatio(force_aspect_ratio);
}

bool
Qt6SWVideoItemInterface::getForceAspectRatio()
{
  QMutexLocker locker(&lock);
  if (!qt_item)
    return FALSE;
  return qt_item->getForceAspectRatio();
}

void
Qt6SWVideoItemInterface::setDAR(gint num, gint den)
{
  QMutexLocker locker(&lock);
  if (!qt_item)
    return;
  qt_item->setDAR(num, den);
}

void
Qt6SWVideoItemInterface::getDAR(gint * num, gint * den)
{
  QMutexLocker locker(&lock);
  if (!qt_item)
    return;
  qt_item->getDAR (num, den);
}
