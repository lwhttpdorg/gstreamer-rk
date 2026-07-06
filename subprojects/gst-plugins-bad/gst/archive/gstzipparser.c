/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
 *
 * gstzipparser.c:
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstzipparser.h"

#include <gst/gstutils.h>

#include <errno.h>
#include <string.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef G_OS_WIN32
#include <io.h>
#undef lseek
#define lseek _lseeki64
#undef off_t
#define off_t guint64
#endif

/* ZIP record signatures and fixed header sizes from PKWARE APPNOTE.TXT:
 * https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
 *
 * Relevant sections:
 * - 4.3.7:  local file header
 * - 4.3.12: central directory file header
 * - 4.3.14: Zip64 end of central directory record
 * - 4.3.15: Zip64 end of central directory locator
 * - 4.3.16: end of central directory record
 * - 4.4.4:  general purpose bit flag, including bit 0 for encryption
 * - 4.4.5:  compression method, including method 0 for "stored"
 * - 4.4.8, 4.4.9, 4.4.13, 4.4.16, 4.4.21-4.4.24:
 *            Zip64 sentinel values for size/count/offset fields
 * - 4.5.3:  Zip64 extended information extra field
 *
 * This parser only implements enough of the format to expose stored entries as
 * bounded byte ranges; it intentionally does not implement decompression.
 */
#define ZIP64_EOCD_SIGNATURE                        0x06064b50
#define ZIP64_EOCD_LOCATOR_SIGNATURE                0x07064b50
#define ZIP_GENERAL_PURPOSE_BIT_FLAG_ENCRYPTED      (1 << 0)
#define ZIP_UINT16_SENTINEL                         0xffff
#define ZIP_EOCD_MIN_SIZE                           22
#define ZIP_EOCD_SEARCH_MAX                         \
    (ZIP_EOCD_MIN_SIZE + ZIP_UINT16_SENTINEL)
#define ZIP_EOCD_COMMENT_LENGTH_OFFSET              20
#define ZIP_LOCAL_FILE_HEADER_SIZE                  30
#define ZIP_CENTRAL_DIRECTORY_FILE_HEADER_SIZE      46
#define ZIP64_EOCD_PREFIX_SIZE                      12
#define ZIP64_EOCD_MIN_REMAINING_SIZE               44

typedef struct
{
  guint64 central_dir_offset;
  guint64 central_dir_size;
  guint64 entries;
} ZipDirectory;

typedef struct
{
  guint8 data[ZIP_EOCD_MIN_SIZE];
  guint64 offset;
} ZipEocd;

typedef struct
{
  guint16 method;
  guint16 flags;
  guint64 compressed_size;
  guint64 uncompressed_size;
  guint64 local_header_offset;
  guint32 disk_number;
} ZipMember;

typedef struct
{
  guint8 header[ZIP_CENTRAL_DIRECTORY_FILE_HEADER_SIZE];
  guint8 *variable;
  guint variable_capacity;
  guint variable_len;
  guint16 name_len;
  guint16 extra_len;
  guint16 comment_len;
  const guint8 *name;
  const guint8 *extra;
} ZipCentralEntry;

static inline guint16
zip_read_le16 (const guint8 * data)
{
  return GST_READ_UINT16_LE (data);
}

static inline guint32
zip_read_le32 (const guint8 * data)
{
  return GST_READ_UINT32_LE (data);
}

static inline guint64
zip_read_le64 (const guint8 * data)
{
  return GST_READ_UINT64_LE (data);
}

/* Return TRUE when the byte range [@offset, @offset + @size) is fully within
 * a container of @limit bytes, without overflowing the addition.
 */
static gboolean
zip_range_fits (guint64 offset, guint64 size, guint64 limit)
{
  return offset <= limit && size <= limit - offset;
}

/* Read @size bytes from @fd into @data at absolute archive @offset, without
 * changing the current file position on platforms with pread(). Returns the
 * number of bytes read, or -1 with errno set.
 */
static gssize
zip_pread (gint fd, guint8 * data, gsize size, guint64 offset)
{
  /* lseek()-style APIs use signed 64-bit offsets. Reject values that cannot be
   * represented before they reach the platform call.
   */
  if (G_UNLIKELY (offset > (guint64) G_MAXINT64)) {
    errno = EINVAL;
    return -1;
  }
#ifdef G_OS_WIN32
  if (lseek (fd, offset, SEEK_SET) == (off_t) - 1)
    return -1;
  return read (fd, data, size);
#else
  return pread (fd, data, size, (off_t) offset);
#endif
}

static gboolean
zip_read_at (gint fd, guint64 offset, guint8 * data, gsize size,
    GError ** error)
{
  gsize done = 0;

  if (G_UNLIKELY (size > G_MAXUINT64 - offset)) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "ZIP archive read offset overflow");
    return FALSE;
  }

  while (done < size) {
    gssize ret;

    ret = zip_pread (fd, data + done, size - done, offset + done);
    if (ret < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
          "Could not read ZIP archive: %s", g_strerror (errno));
      return FALSE;
    }
    if (ret == 0) {
      g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
          "Unexpected end of ZIP archive");
      return FALSE;
    }

    done += ret;
  }

  return TRUE;
}

static gboolean
zip_parse_zip64_extra (const guint8 * extra, guint16 extra_len,
    gboolean need_uncompressed_size, gboolean need_compressed_size,
    gboolean need_local_header_offset, gboolean need_disk_number,
    ZipMember * member, GError ** error)
{
  guint pos = 0;

  /* APPNOTE 4.5.3: Zip64 extra fields are ordered by the corresponding
   * 0xffff/0xffffffff sentinels in the local or central directory record.
   */
  while (extra_len - pos >= 4) {
    guint16 header_id = zip_read_le16 (extra + pos);
    guint16 data_size = zip_read_le16 (extra + pos + 2);
    const guint8 *data = extra + pos + 4;
    guint data_pos = 0;

    pos += 4;
    if (data_size > extra_len - pos)
      break;

    if (header_id == GST_ZIP64_EXTENDED_INFORMATION_EXTRA_FIELD_ID) {
      if (need_uncompressed_size) {
        if (data_size - data_pos < 8)
          goto malformed;
        member->uncompressed_size = zip_read_le64 (data + data_pos);
        data_pos += 8;
      }
      if (need_compressed_size) {
        if (data_size - data_pos < 8)
          goto malformed;
        member->compressed_size = zip_read_le64 (data + data_pos);
        data_pos += 8;
      }
      if (need_local_header_offset) {
        if (data_size - data_pos < 8)
          goto malformed;
        member->local_header_offset = zip_read_le64 (data + data_pos);
        data_pos += 8;
      }
      if (need_disk_number) {
        if (data_size - data_pos < 4)
          goto malformed;
        member->disk_number = zip_read_le32 (data + data_pos);
      }

      return TRUE;
    }

    pos += data_size;
  }

  g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
      "Missing required Zip64 extended information extra field");
  return FALSE;

malformed:
  g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
      "Malformed Zip64 extended information extra field");
  return FALSE;
}

static gboolean
zip_find_eocd (gint fd, guint64 archive_size, ZipEocd * eocd, GError ** error)
{
  guint8 *buf = NULL;
  guint64 search_size;
  guint64 search_offset;
  gint64 eocd_pos = -1;
  gboolean ret = FALSE;

  /* APPNOTE 4.3.16: the EOCD record is at the end of the archive and includes
   * an optional comment of up to 0xffff bytes.
   */
  if (archive_size < ZIP_EOCD_MIN_SIZE) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Archive is too small to be a ZIP file");
    return FALSE;
  }

  eocd->offset = archive_size - ZIP_EOCD_MIN_SIZE;
  if (!zip_read_at (fd, eocd->offset, eocd->data, sizeof (eocd->data), error))
    goto done;

  /* In the common no-comment case, the EOCD is exactly the final
   * ZIP_EOCD_MIN_SIZE bytes. Only fall back to the full comment-length search
   * if the final bytes are not a zero-comment EOCD.
   */
  if (zip_read_le32 (eocd->data) == GST_ZIP_EOCD_SIGNATURE &&
      zip_read_le16 (eocd->data + ZIP_EOCD_COMMENT_LENGTH_OFFSET) == 0) {
    ret = TRUE;
    goto done;
  }

  search_size = MIN ((guint64) ZIP_EOCD_SEARCH_MAX, archive_size);
  search_offset = archive_size - search_size;
  /* Keep the fallback buffer off the stack: the maximum EOCD search window is
   * over 64 KiB, and this path is only needed for archives with comments.
   */
  buf = g_malloc (search_size);
  if (!zip_read_at (fd, search_offset, buf, search_size, error))
    goto done;

  for (gint64 pos = search_size - ZIP_EOCD_MIN_SIZE; pos >= 0; pos--) {
    if (zip_read_le32 (buf + pos) == GST_ZIP_EOCD_SIGNATURE) {
      guint16 comment_len =
          zip_read_le16 (buf + pos + ZIP_EOCD_COMMENT_LENGTH_OFFSET);

      if ((guint64) pos <= search_size - ZIP_EOCD_MIN_SIZE &&
          comment_len == search_size - (guint64) pos - ZIP_EOCD_MIN_SIZE) {
        eocd_pos = pos;
        break;
      }
    }
  }

  if (eocd_pos < 0) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Could not find ZIP end of central directory");
    goto done;
  }

  memcpy (eocd->data, buf + eocd_pos, sizeof (eocd->data));
  eocd->offset = search_offset + eocd_pos;
  ret = TRUE;

done:
  g_free (buf);
  return ret;
}

static gboolean
zip_parse_eocd (const ZipEocd * eocd, ZipDirectory * dir,
    gboolean * needs_zip64, GError ** error)
{
  guint16 disk_number = zip_read_le16 (eocd->data + 4);
  guint16 central_dir_disk = zip_read_le16 (eocd->data + 6);
  guint32 entries32 = zip_read_le16 (eocd->data + 10);
  guint32 central_dir_size32 = zip_read_le32 (eocd->data + 12);
  guint32 central_dir_offset32 = zip_read_le32 (eocd->data + 16);

  if (disk_number != 0 || central_dir_disk != 0) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Multi-disk ZIP archives are not supported");
    return FALSE;
  }

  *needs_zip64 = entries32 == ZIP_UINT16_SENTINEL ||
      central_dir_size32 == GST_ZIP_UINT32_SENTINEL ||
      central_dir_offset32 == GST_ZIP_UINT32_SENTINEL;

  if (!*needs_zip64) {
    dir->entries = entries32;
    dir->central_dir_size = central_dir_size32;
    dir->central_dir_offset = central_dir_offset32;
  }

  return TRUE;
}

static gboolean
zip_parse_zip64_directory (gint fd, guint64 archive_size, const ZipEocd * eocd,
    ZipDirectory * dir, GError ** error)
{
  guint8 locator[20];
  guint8 zip64_eocd[56];
  guint64 zip64_eocd_offset;
  guint64 zip64_eocd_size;
  guint64 zip64_eocd_total_size;
  guint32 total_disks;

  if (eocd->offset < sizeof (locator)) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Missing Zip64 end of central directory locator");
    return FALSE;
  }

  /* APPNOTE 4.3.15: Zip64 EOCD locator immediately precedes the EOCD. */
  if (!zip_read_at (fd, eocd->offset - sizeof (locator), locator,
          sizeof (locator), error))
    return FALSE;

  if (zip_read_le32 (locator) != ZIP64_EOCD_LOCATOR_SIGNATURE) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Missing Zip64 end of central directory locator");
    return FALSE;
  }

  /* APPNOTE 4.3.15: fixed Zip64 EOCD locator fields used here:
   * +4: disk with the Zip64 EOCD record start (4 bytes)
   * +8: Zip64 EOCD record offset (8 bytes)
   * +16: total number of disks (4 bytes)
   */
  if (zip_read_le32 (locator + 4) != 0) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Multi-disk Zip64 archives are not supported");
    return FALSE;
  }

  zip64_eocd_offset = zip_read_le64 (locator + 8);
  total_disks = zip_read_le32 (locator + 16);
  if (total_disks != 1) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Multi-disk Zip64 archives are not supported");
    return FALSE;
  }
  if (!zip_range_fits (zip64_eocd_offset, sizeof (zip64_eocd), archive_size)) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Zip64 end of central directory record lies outside the archive");
    return FALSE;
  }

  /* APPNOTE 4.3.14: Zip64 EOCD record contains 64-bit central directory
   * entry count, size, and offset values.
   */
  if (!zip_read_at (fd, zip64_eocd_offset, zip64_eocd,
          sizeof (zip64_eocd), error))
    return FALSE;

  if (zip_read_le32 (zip64_eocd) != ZIP64_EOCD_SIGNATURE) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Missing Zip64 end of central directory record");
    return FALSE;
  }

  zip64_eocd_size = zip_read_le64 (zip64_eocd + 4);
  /* The Zip64 EOCD size field excludes the 4-byte signature and the 8-byte
   * size field itself. The fixed remainder after those fields is 44 bytes.
   */
  if (zip64_eocd_size < ZIP64_EOCD_MIN_REMAINING_SIZE) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Malformed Zip64 end of central directory record");
    return FALSE;
  }
  if (!g_uint64_checked_add (&zip64_eocd_total_size, ZIP64_EOCD_PREFIX_SIZE,
          zip64_eocd_size) ||
      !zip_range_fits (zip64_eocd_offset, zip64_eocd_total_size,
          archive_size)) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Zip64 end of central directory record exceeds archive size");
    return FALSE;
  }
  /* Fixed Zip64 EOCD fields used here:
   * +16: number of this disk (4 bytes)
   * +20: number of disk with central directory start (4 bytes)
   * +32: total central directory entries (8 bytes)
   * +40: central directory size (8 bytes)
   * +48: central directory offset (8 bytes)
   */
  if (zip_read_le32 (zip64_eocd + 16) != 0 ||
      zip_read_le32 (zip64_eocd + 20) != 0) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Multi-disk Zip64 archives are not supported");
    return FALSE;
  }

  dir->entries = zip_read_le64 (zip64_eocd + 32);
  dir->central_dir_size = zip_read_le64 (zip64_eocd + 40);
  dir->central_dir_offset = zip_read_le64 (zip64_eocd + 48);

  return TRUE;
}

static gboolean
zip_validate_directory (guint64 archive_size, const ZipDirectory * dir,
    GError ** error)
{
  if (dir->central_dir_offset > archive_size ||
      dir->central_dir_size > archive_size - dir->central_dir_offset) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "ZIP central directory lies outside the archive");
    return FALSE;
  }

  return TRUE;
}

static gboolean
zip_parse_directory (gint fd, guint64 archive_size, ZipDirectory * dir,
    GError ** error)
{
  ZipEocd eocd;
  gboolean needs_zip64;

  if (!zip_find_eocd (fd, archive_size, &eocd, error))
    return FALSE;
  if (!zip_parse_eocd (&eocd, dir, &needs_zip64, error))
    return FALSE;

  /* APPNOTE 4.4.1.4 and 4.4.21-4.4.24: EOCD fields that are too small are
   * filled with 0xffff/0xffffffff and replaced by the Zip64 EOCD values.
   */
  if (needs_zip64 && !zip_parse_zip64_directory (fd, archive_size, &eocd, dir,
          error))
    return FALSE;

  return zip_validate_directory (archive_size, dir, error);
}

static gboolean
zip_read_central_entry (gint fd, guint64 offset, guint64 central_dir_end,
    ZipCentralEntry * entry, GError ** error)
{
  if (offset > central_dir_end ||
      central_dir_end - offset < ZIP_CENTRAL_DIRECTORY_FILE_HEADER_SIZE) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "ZIP central directory entry exceeds central directory size");
    return FALSE;
  }

  if (!zip_read_at (fd, offset, entry->header, sizeof (entry->header), error))
    return FALSE;

  if (zip_read_le32 (entry->header) !=
      GST_ZIP_CENTRAL_DIRECTORY_FILE_HEADER_SIGNATURE) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Malformed ZIP central directory");
    return FALSE;
  }

  /* APPNOTE 4.3.12: fixed central directory file header fields used here:
   * +28: file name length (2 bytes)
   * +30: extra field length (2 bytes)
   * +32: file comment length (2 bytes)
   */
  entry->name_len = zip_read_le16 (entry->header + 28);
  entry->extra_len = zip_read_le16 (entry->header + 30);
  entry->comment_len = zip_read_le16 (entry->header + 32);
  entry->variable_len =
      (guint) entry->name_len + entry->extra_len + entry->comment_len;

  if (central_dir_end - offset -
      ZIP_CENTRAL_DIRECTORY_FILE_HEADER_SIZE < entry->variable_len) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "ZIP central directory entry exceeds central directory size");
    return FALSE;
  }

  if (entry->variable_capacity < MAX (entry->variable_len, 1)) {
    entry->variable_capacity = MAX (entry->variable_len, 1);
    entry->variable = g_realloc (entry->variable, entry->variable_capacity);
  }

  if (!zip_read_at (fd, offset + ZIP_CENTRAL_DIRECTORY_FILE_HEADER_SIZE,
          entry->variable, entry->variable_len, error))
    return FALSE;

  entry->name = entry->variable;
  entry->extra = entry->variable + entry->name_len;

  return TRUE;
}

static gboolean
zip_central_entry_matches (const ZipCentralEntry * entry,
    const gchar * location, gsize location_len)
{
  return location_len == entry->name_len &&
      memcmp (location, entry->name, entry->name_len) == 0;
}

static gboolean
zip_member_from_central_entry (const ZipCentralEntry * entry,
    ZipMember * member, GError ** error)
{
  ZipMember parsed = { 0 };
  gboolean need_uncompressed_size;
  gboolean need_compressed_size;
  gboolean need_local_header_offset;
  gboolean need_disk_number;

  /* APPNOTE 4.3.12: fixed central directory file header fields used here:
   * +8: general purpose bit flag (2 bytes)
   * +10: compression method (2 bytes)
   * +20: compressed size (4 bytes)
   * +24: uncompressed size (4 bytes)
   * +34: disk number start (2 bytes)
   * +42: relative offset of local header (4 bytes)
   */
  parsed.flags = zip_read_le16 (entry->header + 8);
  parsed.method = zip_read_le16 (entry->header + 10);
  parsed.compressed_size = zip_read_le32 (entry->header + 20);
  parsed.uncompressed_size = zip_read_le32 (entry->header + 24);
  parsed.disk_number = zip_read_le16 (entry->header + 34);
  parsed.local_header_offset = zip_read_le32 (entry->header + 42);

  /* APPNOTE 4.4.8, 4.4.9, 4.4.13, and 4.4.16: these fields use
   * 0xffffffff/0xffff sentinels when their values are stored in the Zip64
   * extended information extra field.
   */
  need_uncompressed_size = parsed.uncompressed_size == GST_ZIP_UINT32_SENTINEL;
  need_compressed_size = parsed.compressed_size == GST_ZIP_UINT32_SENTINEL;
  need_local_header_offset =
      parsed.local_header_offset == GST_ZIP_UINT32_SENTINEL;
  need_disk_number = parsed.disk_number == ZIP_UINT16_SENTINEL;

  if (need_uncompressed_size || need_compressed_size ||
      need_local_header_offset || need_disk_number) {
    if (!zip_parse_zip64_extra (entry->extra, entry->extra_len,
            need_uncompressed_size, need_compressed_size,
            need_local_header_offset, need_disk_number, &parsed, error))
      return FALSE;
  }

  *member = parsed;
  return TRUE;
}

static gboolean
zip_find_member (gint fd, const gchar * location, const ZipDirectory * dir,
    ZipMember * member, GError ** error)
{
  guint64 offset = dir->central_dir_offset;
  guint64 central_dir_end = dir->central_dir_offset + dir->central_dir_size;
  gsize location_len = strlen (location);
  ZipCentralEntry entry = { 0 };
  gboolean ret = FALSE;

  /* APPNOTE 4.3.12: central directory file headers carry the authoritative
   * member metadata needed to locate and validate a stored entry.
   */
  for (guint64 i = 0; i < dir->entries; i++) {
    if (!zip_read_central_entry (fd, offset, central_dir_end, &entry, error))
      goto done;

    if (zip_central_entry_matches (&entry, location, location_len)) {
      ret = zip_member_from_central_entry (&entry, member, error);
      goto done;
    }

    offset += ZIP_CENTRAL_DIRECTORY_FILE_HEADER_SIZE + entry.variable_len;
  }

  g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT,
      "ZIP member '%s' was not found", location);

done:
  g_free (entry.variable);
  return ret;
}

static gboolean
zip_resolve_member_payload (gint fd, guint64 archive_size,
    const gchar * location, const ZipMember * member, GstZipEntryRange * range,
    GError ** error)
{
  guint8 header[ZIP_LOCAL_FILE_HEADER_SIZE];
  guint16 name_len, extra_len;
  guint64 payload_offset;

  if (member->disk_number != 0) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Multi-disk ZIP entries are not supported");
    return FALSE;
  }
  /* APPNOTE 4.4.4 bit 0: encrypted entry. */
  if (member->flags & ZIP_GENERAL_PURPOSE_BIT_FLAG_ENCRYPTED) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Encrypted ZIP entries are not supported");
    return FALSE;
  }
  /* APPNOTE 4.4.5 method 0: stored, no compression. */
  if (member->method != GST_ZIP_COMPRESSION_METHOD_STORE) {
    g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "ZIP member '%s' uses compression method %u, only stored entries are supported",
        location, member->method);
    return FALSE;
  }
  if (member->compressed_size != member->uncompressed_size) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Stored ZIP entry has different compressed and uncompressed sizes");
    return FALSE;
  }

  /* APPNOTE 4.3.7: local file header immediately precedes the entry payload. */
  if (!zip_range_fits (member->local_header_offset, sizeof (header),
          archive_size)) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "ZIP local file header lies outside the archive");
    return FALSE;
  }

  if (!zip_read_at (fd, member->local_header_offset, header, sizeof (header),
          error))
    return FALSE;
  if (zip_read_le32 (header) != GST_ZIP_LOCAL_FILE_HEADER_SIGNATURE) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Malformed ZIP local file header");
    return FALSE;
  }

  name_len = zip_read_le16 (header + 26);
  extra_len = zip_read_le16 (header + 28);
  if (!g_uint64_checked_add (&payload_offset, member->local_header_offset,
          ZIP_LOCAL_FILE_HEADER_SIZE) ||
      !g_uint64_checked_add (&payload_offset, payload_offset, name_len) ||
      !g_uint64_checked_add (&payload_offset, payload_offset, extra_len)) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "ZIP member payload offset overflow");
    return FALSE;
  }

  range->offset = payload_offset;
  range->size = member->uncompressed_size;

  if (!zip_range_fits (range->offset, range->size, archive_size)) {
    g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "ZIP member payload lies outside the archive");
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_zip_parser_find_stored_entry (gint fd, guint64 archive_size,
    const gchar * location, GstZipEntryRange * range, GError ** error)
{
  ZipDirectory dir = { 0 };
  ZipMember member = { 0 };

  g_return_val_if_fail (fd >= 0, FALSE);
  g_return_val_if_fail (location != NULL, FALSE);
  g_return_val_if_fail (range != NULL, FALSE);

  if (!zip_parse_directory (fd, archive_size, &dir, error))
    return FALSE;
  if (!zip_find_member (fd, location, &dir, &member, error))
    return FALSE;

  return zip_resolve_member_payload (fd, archive_size, location, &member, range,
      error);
}
