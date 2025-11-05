#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstMovpkgMoviePackage GstMovpkgMoviePackage;

typedef enum {
  GST_MOVPKG_MOVIE_PACKAGE_TYPE_NONE,
  GST_MOVPKG_MOVIE_PACKAGE_TYPE_HLS,
} GstMovpkgMoviePackageType;

gboolean
gst_movpkg_parse_movie_package (GstMovpkgMoviePackage ** pkg,
                                const gchar            * data,
                                gsize                    size,
                                GError                ** error);

void
gst_movpkg_movie_package_free (GstMovpkgMoviePackage * self);

const gchar *
gst_movpkg_movie_package_get_boot_image (GstMovpkgMoviePackage * self);

G_END_DECLS
