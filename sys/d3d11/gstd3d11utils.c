/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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
#include "config.h"
#endif

#include "gstd3d11utils.h"
#include "gstd3d11device.h"
#include "gstd3d11bufferpool.h"

#include <windows.h>
#include <versionhelpers.h>

GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);
GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_utils_debug);
#define GST_CAT_DEFAULT gst_d3d11_utils_debug

static void
_init_context_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
    g_once_init_leave (&_init, 1);
  }
}

/**
 * gst_d3d11_handle_set_context:
 * @element: a #GstElement
 * @context: a #GstContext
 * @device: (inout) (transfer full): location of a #GstD3DDevice
 *
 * Helper function for implementing #GstElementClass.set_context() in
 * D3D11 capable elements.
 *
 * Retrieve's the #GstD3D11Device in @context and places the result in @device.
 *
 * Returns: whether the @device could be set successfully
 */
gboolean
gst_d3d11_handle_set_context (GstElement * element, GstContext * context,
    gint adapter, GstD3D11Device ** device)
{
  const gchar *context_type;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  _init_context_debug ();

  if (!context)
    return FALSE;

  context_type = gst_context_get_context_type (context);
  if (g_strcmp0 (context_type, GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE) == 0) {
    const GstStructure *str;
    GstD3D11Device *other_device = NULL;
    guint other_adapter = 0;

    /* If we had device already, will not replace it */
    if (*device)
      return TRUE;

    str = gst_context_get_structure (context);

    if (gst_structure_get (str, "device", GST_TYPE_D3D11_DEVICE,
            &other_device, "adapter", G_TYPE_UINT, &other_adapter, NULL)) {
      if (adapter == -1 || (guint) adapter == other_adapter) {
        GST_CAT_DEBUG_OBJECT (GST_CAT_CONTEXT,
            element, "Found D3D11 device context");
        *device = other_device;

        return TRUE;
      }

      gst_object_unref (other_device);
    }
  }

  return FALSE;
}

static void
context_set_d3d11_device (GstContext * context, GstD3D11Device * device)
{
  GstStructure *s;
  guint adapter = 0;
  guint device_id = 0;
  guint vendor_id = 0;
  gboolean hardware = FALSE;
  gchar *desc = NULL;
  gint64 adapter_luid = 0;

  g_return_if_fail (context != NULL);

  g_object_get (G_OBJECT (device), "adapter", &adapter, "device-id", &device_id,
      "vendor-id", &vendor_id, "hardware", &hardware, "description", &desc,
      "adapter-luid", &adapter_luid, NULL);

  GST_CAT_LOG (GST_CAT_CONTEXT,
      "setting GstD3D11Device(%" GST_PTR_FORMAT
      ") with adapter %d on context(%" GST_PTR_FORMAT ")",
      device, adapter, context);

  s = gst_context_writable_structure (context);
  gst_structure_set (s, "device", GST_TYPE_D3D11_DEVICE, device,
      "adapter", G_TYPE_UINT, adapter,
      "adapter-luid", G_TYPE_INT64, adapter_luid,
      "device-id", G_TYPE_UINT, device_id,
      "vendor-id", G_TYPE_UINT, vendor_id,
      "hardware", G_TYPE_BOOLEAN, hardware,
      "description", G_TYPE_STRING, GST_STR_NULL (desc), NULL);
  g_free (desc);
}

/**
 * gst_d3d11_handle_context_query:
 * @element: a #GstElement
 * @query: a #GstQuery of type %GST_QUERY_CONTEXT
 * @device: (transfer none) (nullable): a #GstD3D11Device
 *
 * Returns: Whether the @query was successfully responded to from the passed
 *          @device.
 */
gboolean
gst_d3d11_handle_context_query (GstElement * element, GstQuery * query,
    GstD3D11Device * device)
{
  const gchar *context_type;
  GstContext *context, *old_context;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_QUERY (query), FALSE);

  _init_context_debug ();

  GST_LOG_OBJECT (element, "handle context query %" GST_PTR_FORMAT, query);

  if (!device)
    return FALSE;

  gst_query_parse_context_type (query, &context_type);
  if (g_strcmp0 (context_type, GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE) != 0)
    return FALSE;

  gst_query_parse_context (query, &old_context);
  if (old_context)
    context = gst_context_copy (old_context);
  else
    context = gst_context_new (GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE, TRUE);

  context_set_d3d11_device (context, device);
  gst_query_set_context (query, context);
  gst_context_unref (context);

  GST_DEBUG_OBJECT (element, "successfully set %" GST_PTR_FORMAT
      " on %" GST_PTR_FORMAT, device, query);

  return TRUE;
}

static gboolean
pad_query (const GValue * item, GValue * value, gpointer user_data)
{
  GstPad *pad = g_value_get_object (item);
  GstQuery *query = user_data;
  gboolean res;

  res = gst_pad_peer_query (pad, query);

  if (res) {
    g_value_set_boolean (value, TRUE);
    return FALSE;
  }

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, pad, "pad peer query failed");
  return TRUE;
}

static gboolean
run_query (GstElement * element, GstQuery * query, GstPadDirection direction)
{
  GstIterator *it;
  GstIteratorFoldFunction func = pad_query;
  GValue res = { 0 };

  g_value_init (&res, G_TYPE_BOOLEAN);
  g_value_set_boolean (&res, FALSE);

  /* Ask neighbor */
  if (direction == GST_PAD_SRC)
    it = gst_element_iterate_src_pads (element);
  else
    it = gst_element_iterate_sink_pads (element);

  while (gst_iterator_fold (it, func, &res, query) == GST_ITERATOR_RESYNC)
    gst_iterator_resync (it);

  gst_iterator_free (it);

  return g_value_get_boolean (&res);
}

static void
run_d3d11_context_query (GstElement * element, GstD3D11Device ** device)
{
  GstQuery *query;
  GstContext *ctxt = NULL;

  /* 1) Query downstream with GST_QUERY_CONTEXT for the context and
   *    check if downstream already has a context of the specific type
   */
  query = gst_query_new_context (GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE);
  if (run_query (element, query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &ctxt);
    if (ctxt) {
      GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
          "found context (%" GST_PTR_FORMAT ") in downstream query", ctxt);
      gst_element_set_context (element, ctxt);
    }
  }

  /* 2) although we found d3d11 device context above, the element does not want
   *    to use the context. Then try to find from the other direction */
  if (*device == NULL && run_query (element, query, GST_PAD_SINK)) {
    gst_query_parse_context (query, &ctxt);
    if (ctxt) {
      GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
          "found context (%" GST_PTR_FORMAT ") in upstream query", ctxt);
      gst_element_set_context (element, ctxt);
    }
  }

  if (*device == NULL) {
    /* 3) Post a GST_MESSAGE_NEED_CONTEXT message on the bus with
     *    the required context type and afterwards check if a
     *    usable context was set now as in 1). The message could
     *    be handled by the parent bins of the element and the
     *    application.
     */
    GstMessage *msg;

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting need context message");
    msg = gst_message_new_need_context (GST_OBJECT_CAST (element),
        GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE);
    gst_element_post_message (element, msg);
  }

  /*
   * Whomever responds to the need-context message performs a
   * GstElement::set_context() with the required context in which the element
   * is required to update the display_ptr or call gst_gl_handle_set_context().
   */

  gst_query_unref (query);
}

/**
 * gst_d3d11_ensure_element_data:
 * @element: the #GstElement running the query
 * @adapter: prefered adapter index, pass adapter >=0 when
 *           the adapter explicitly required. Otherwise, set -1.
 * @device: (inout): the resulting #GstD3D11Device
 *
 * Perform the steps necessary for retrieving a #GstD3D11Device
 * from the surrounding elements or from the application using the #GstContext mechanism.
 *
 * If the contents of @device is not %NULL, then no #GstContext query is
 * necessary for #GstD3D11Device retrieval is performed.
 *
 * Returns: whether a #GstD3D11Device exists in @device
 */
gboolean
gst_d3d11_ensure_element_data (GstElement * element, gint adapter,
    GstD3D11Device ** device)
{
  guint target_adapter = 0;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  _init_context_debug ();

  if (*device) {
    GST_LOG_OBJECT (element, "already have a device %" GST_PTR_FORMAT, *device);
    return TRUE;
  }

  run_d3d11_context_query (element, device);
  if (*device)
    return TRUE;

  if (adapter > 0)
    target_adapter = adapter;

  /* Needs D3D11_CREATE_DEVICE_BGRA_SUPPORT flag for Direct2D interop */
  *device = gst_d3d11_device_new (target_adapter,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT);

  if (*device == NULL) {
    GST_ERROR_OBJECT (element,
        "Couldn't create new device with adapter index %d", target_adapter);
    return FALSE;
  } else {
    GstContext *context;
    GstMessage *msg;

    /* Propagate new D3D11 device context */

    context = gst_context_new (GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE, TRUE);
    context_set_d3d11_device (context, *device);

    gst_element_set_context (element, context);

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting have context (%p) message with D3D11 device context (%p)",
        context, *device);
    msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
    gst_element_post_message (GST_ELEMENT_CAST (element), msg);
  }

  return TRUE;
}

gboolean
gst_d3d11_is_windows_8_or_greater (void)
{
  static gsize version_once = 0;
  static gboolean ret = FALSE;

  if (g_once_init_enter (&version_once)) {
#if (!GST_D3D11_WINAPI_ONLY_APP)
    if (IsWindows8OrGreater ())
      ret = TRUE;
#else
    ret = TRUE;
#endif

    g_once_init_leave (&version_once, 1);
  }

  return ret;
}

GstD3D11DeviceVendor
gst_d3d11_get_device_vendor (GstD3D11Device * device)
{
  guint device_id = 0;
  guint vendor_id = 0;
  gchar *desc = NULL;
  GstD3D11DeviceVendor vendor = GST_D3D11_DEVICE_VENDOR_UNKNOWN;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), FALSE);

  g_object_get (device, "device-id", &device_id, "vendor-id", &vendor_id,
      "description", &desc, NULL);

  switch (vendor_id) {
    case 0:
      if (device_id == 0 && desc && g_strrstr (desc, "SraKmd"))
        vendor = GST_D3D11_DEVICE_VENDOR_XBOX;
      break;
    case 0x1002:
    case 0x1022:
      vendor = GST_D3D11_DEVICE_VENDOR_AMD;
      break;
    case 0x8086:
      vendor = GST_D3D11_DEVICE_VENDOR_INTEL;
      break;
    case 0x10de:
      vendor = GST_D3D11_DEVICE_VENDOR_NVIDIA;
      break;
    case 0x4d4f4351:
      vendor = GST_D3D11_DEVICE_VENDOR_QUALCOMM;
      break;
    default:
      break;
  }

  g_free (desc);

  return vendor;
}

GstBuffer *
gst_d3d11_allocate_staging_buffer (GstD3D11Allocator * allocator,
    const GstVideoInfo * info, const GstD3D11Format * format,
    const D3D11_TEXTURE2D_DESC desc[GST_VIDEO_MAX_PLANES],
    gboolean add_videometa)
{
  GstBuffer *buffer;
  gint i;
  gint stride[GST_VIDEO_MAX_PLANES] = { 0, };
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };
  GstMemory *mem;

  g_return_val_if_fail (GST_IS_D3D11_ALLOCATOR (allocator), NULL);
  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (format != NULL, NULL);
  g_return_val_if_fail (desc != NULL, NULL);

  buffer = gst_buffer_new ();

  if (format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    gsize size[GST_VIDEO_MAX_PLANES] = { 0, };

    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
      mem = gst_d3d11_allocator_alloc_staging (allocator, &desc[i], 0,
          &stride[i]);

      if (!mem) {
        GST_ERROR_OBJECT (allocator, "Couldn't allocate memory for plane %d",
            i);
        goto error;
      }

      size[i] = gst_memory_get_sizes (mem, NULL, NULL);
      if (i > 0)
        offset[i] = offset[i - 1] + size[i - 1];
      gst_buffer_append_memory (buffer, mem);
    }
  } else {
    /* must be YUV semi-planar or single plane */
    g_assert (GST_VIDEO_INFO_N_PLANES (info) <= 2);

    mem = gst_d3d11_allocator_alloc_staging (allocator, &desc[0], 0,
        &stride[0]);

    if (!mem) {
      GST_ERROR_OBJECT (allocator, "Couldn't allocate memory");
      goto error;
    }

    gst_memory_get_sizes (mem, NULL, NULL);
    gst_buffer_append_memory (buffer, mem);

    if (GST_VIDEO_INFO_N_PLANES (info) == 2) {
      stride[1] = stride[0];
      offset[1] = stride[0] * desc[0].Height;
    }
  }

  if (add_videometa) {
    gst_buffer_add_video_meta_full (buffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        offset, stride);
  }

  return buffer;

error:
  gst_buffer_unref (buffer);

  return NULL;
}

GstBuffer *
gst_d3d11_allocate_staging_buffer_for (GstBuffer * buffer,
    const GstVideoInfo * info, gboolean add_videometa)
{
  GstD3D11Memory *dmem;
  GstD3D11Device *device;
  GstD3D11AllocationParams *params = NULL;
  GstD3D11Allocator *alloc = NULL;
  GstBuffer *staging_buffer = NULL;
  D3D11_TEXTURE2D_DESC *desc;
  gint i;

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    if (!gst_is_d3d11_memory (mem)) {
      GST_DEBUG ("Not a d3d11 memory");

      return NULL;
    }
  }

  dmem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, 0);
  device = dmem->device;

  params = gst_d3d11_allocation_params_new (device, (GstVideoInfo *) info,
      0, 0);

  if (!params) {
    GST_WARNING ("Couldn't create alloc params");
    goto done;
  }

  desc = &params->desc[0];
  /* resolution of semi-planar formats must be multiple of 2 */
  if (desc[0].Format == DXGI_FORMAT_NV12 || desc[0].Format == DXGI_FORMAT_P010
      || desc[0].Format == DXGI_FORMAT_P016) {
    if (desc[0].Width % 2 || desc[0].Height % 2) {
      gint width, height;
      GstVideoAlignment align;

      width = GST_ROUND_UP_2 (desc[0].Width);
      height = GST_ROUND_UP_2 (desc[0].Height);

      gst_video_alignment_reset (&align);
      align.padding_right = width - desc[0].Width;
      align.padding_bottom = height - desc[0].Height;

      gst_d3d11_allocation_params_alignment (params, &align);
    }
  }

  alloc = gst_d3d11_allocator_new (device);
  if (!alloc) {
    GST_WARNING ("Couldn't create allocator");
    goto done;
  }

  staging_buffer = gst_d3d11_allocate_staging_buffer (alloc,
      info, params->d3d11_format, params->desc, add_videometa);

  if (!staging_buffer)
    GST_WARNING ("Couldn't allocate staging buffer");

done:
  if (params)
    gst_d3d11_allocation_params_free (params);

  if (alloc)
    gst_object_unref (alloc);

  return staging_buffer;
}

gboolean
_gst_d3d11_result (HRESULT hr, GstD3D11Device * device, GstDebugCategory * cat,
    const gchar * file, const gchar * function, gint line)
{
#ifndef GST_DISABLE_GST_DEBUG
  gboolean ret = TRUE;

  if (FAILED (hr)) {
    gchar *error_text = NULL;

    error_text = g_win32_error_message ((guint) hr);
    /* g_win32_error_message() doesn't cover all HERESULT return code,
     * so it could be empty string, or null if there was an error
     * in g_utf16_to_utf8() */
    gst_debug_log (cat, GST_LEVEL_WARNING, file, function, line,
        NULL, "D3D11 call failed: 0x%x, %s", (guint) hr,
        GST_STR_NULL (error_text));
    g_free (error_text);

    ret = FALSE;
  }
#if (HAVE_D3D11SDKLAYERS_H || HAVE_DXGIDEBUG_H)
  if (device) {
    gst_d3d11_device_d3d11_debug (device, file, function, line);
    gst_d3d11_device_dxgi_debug (device, file, function, line);
  }
#endif

  return ret;
#else
  return SUCCEEDED (hr);
#endif
}

static gboolean
gst_d3d11_buffer_copy_into_fallback (GstBuffer * dst, GstBuffer * src,
    const GstVideoInfo * info)
{
  GstVideoFrame in_frame, out_frame;
  gboolean ret;

  if (!gst_video_frame_map (&in_frame, (GstVideoInfo *) info, src,
          GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))
    goto invalid_buffer;

  if (!gst_video_frame_map (&out_frame, (GstVideoInfo *) info, dst,
          GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    GST_ERROR ("Invalid video buffer");
    return FALSE;
  }
}

gboolean
gst_d3d11_buffer_copy_into (GstBuffer * dst, GstBuffer * src,
    const GstVideoInfo * info)
{
  guint i;

  g_return_val_if_fail (GST_IS_BUFFER (dst), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (src), FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  if (gst_buffer_n_memory (dst) != gst_buffer_n_memory (src)) {
    GST_LOG ("different memory layout, perform fallback copy");
    return gst_d3d11_buffer_copy_into_fallback (dst, src, info);
  }

  if (!gst_is_d3d11_buffer (dst) || !gst_is_d3d11_buffer (src)) {
    GST_LOG ("non-d3d11 memory, perform fallback copy");
    return gst_d3d11_buffer_copy_into_fallback (dst, src, info);
  }

  for (i = 0; i < gst_buffer_n_memory (dst); i++) {
    GstMemory *dst_mem, *src_mem;
    GstD3D11Memory *dst_dmem, *src_dmem;
    GstMapInfo dst_info;
    GstMapInfo src_info;
    ID3D11Resource *dst_texture, *src_texture;
    ID3D11DeviceContext *device_context;
    GstD3D11Device *device;
    D3D11_BOX src_box = { 0, };
    D3D11_TEXTURE2D_DESC dst_desc, src_desc;
    guint dst_subidx, src_subidx;

    dst_mem = gst_buffer_peek_memory (dst, i);
    src_mem = gst_buffer_peek_memory (src, i);

    dst_dmem = (GstD3D11Memory *) dst_mem;
    src_dmem = (GstD3D11Memory *) src_mem;

    device = dst_dmem->device;
    if (device != src_dmem->device) {
      GST_LOG ("different device, perform fallback copy");
      return gst_d3d11_buffer_copy_into_fallback (dst, src, info);
    }

    gst_d3d11_memory_get_texture_desc (dst_dmem, &dst_desc);
    gst_d3d11_memory_get_texture_desc (src_dmem, &src_desc);

    if (dst_desc.Format != src_desc.Format) {
      GST_WARNING ("different dxgi format");
      return FALSE;
    }

    device_context = gst_d3d11_device_get_device_context_handle (device);

    if (!gst_memory_map (dst_mem, &dst_info, GST_MAP_WRITE | GST_MAP_D3D11)) {
      GST_ERROR ("Cannot map dst d3d11 memory");
      return FALSE;
    }

    if (!gst_memory_map (src_mem, &src_info, GST_MAP_READ | GST_MAP_D3D11)) {
      GST_ERROR ("Cannot map src d3d11 memory");
      gst_memory_unmap (dst_mem, &dst_info);
      return FALSE;
    }

    dst_texture = (ID3D11Resource *) dst_info.data;
    src_texture = (ID3D11Resource *) src_info.data;

    /* src/dst texture size might be different if padding was used.
     * select smaller size */
    src_box.left = 0;
    src_box.top = 0;
    src_box.front = 0;
    src_box.back = 1;
    src_box.right = MIN (src_desc.Width, dst_desc.Width);
    src_box.bottom = MIN (src_desc.Height, dst_desc.Height);

    dst_subidx = gst_d3d11_memory_get_subresource_index (dst_dmem);
    src_subidx = gst_d3d11_memory_get_subresource_index (src_dmem);

    gst_d3d11_device_lock (device);
    ID3D11DeviceContext_CopySubresourceRegion (device_context,
        dst_texture, dst_subidx, 0, 0, 0, src_texture, src_subidx, &src_box);
    gst_d3d11_device_unlock (device);

    gst_memory_unmap (src_mem, &src_info);
    gst_memory_unmap (dst_mem, &dst_info);
  }

  return TRUE;
}

gboolean
gst_is_d3d11_buffer (GstBuffer * buffer)
{
  guint i;
  guint size;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  size = gst_buffer_n_memory (buffer);
  if (size == 0)
    return FALSE;

  for (i = 0; i < size; i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    if (!gst_is_d3d11_memory (mem))
      return FALSE;
  }

  return TRUE;
}

gboolean
gst_d3d11_buffer_can_access_device (GstBuffer * buffer, ID3D11Device * device)
{
  guint i;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  if (!gst_is_d3d11_buffer (buffer)) {
    GST_LOG ("Not a d3d11 buffer");
    return FALSE;
  }

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    ID3D11Device *handle;

    handle = gst_d3d11_device_get_device_handle (mem->device);
    if (handle != device) {
      GST_LOG ("D3D11 device is incompatible");
      return FALSE;
    }
  }

  return TRUE;
}

gboolean
gst_d3d11_buffer_map (GstBuffer * buffer, ID3D11Device * device,
    GstMapInfo info[GST_VIDEO_MAX_PLANES], GstMapFlags flags)
{
  GstMapFlags map_flags;
  gint num_mapped = 0;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  if (!gst_d3d11_buffer_can_access_device (buffer, device))
    return FALSE;

  map_flags = flags | GST_MAP_D3D11;

  for (num_mapped = 0; num_mapped < gst_buffer_n_memory (buffer); num_mapped++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, num_mapped);

    if (!gst_memory_map (mem, &info[num_mapped], map_flags)) {
      GST_ERROR ("Couldn't map memory");
      goto error;
    }
  }

  return TRUE;

error:
  {
    gint i;
    for (i = 0; i < num_mapped; i++) {
      GstMemory *mem = gst_buffer_peek_memory (buffer, num_mapped);
      gst_memory_unmap (mem, &info[i]);
    }

    return FALSE;
  }
}

gboolean
gst_d3d11_buffer_unmap (GstBuffer * buffer,
    GstMapInfo info[GST_VIDEO_MAX_PLANES])
{
  gint i;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    gst_memory_unmap (mem, &info[i]);
  }

  return TRUE;
}

guint
gst_d3d11_buffer_get_shader_resource_view (GstBuffer * buffer,
    ID3D11ShaderResourceView * view[GST_VIDEO_MAX_PLANES])
{
  gint i;
  guint num_views = 0;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (view != NULL, 0);

  if (!gst_is_d3d11_buffer (buffer)) {
    GST_ERROR ("Buffer contains non-d3d11 memory");
    return 0;
  }

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    guint view_size;
    gint j;

    view_size = gst_d3d11_memory_get_shader_resource_view_size (mem);
    if (!view_size) {
      GST_LOG ("SRV is unavailable for memory index %d", i);
      return 0;
    }

    for (j = 0; j < view_size; j++) {
      if (num_views >= GST_VIDEO_MAX_PLANES) {
        GST_ERROR ("Too many SRVs");
        return 0;
      }

      view[num_views++] = gst_d3d11_memory_get_shader_resource_view (mem, j);
    }
  }

  return num_views;
}

guint
gst_d3d11_buffer_get_render_target_view (GstBuffer * buffer,
    ID3D11RenderTargetView * view[GST_VIDEO_MAX_PLANES])
{
  gint i;
  guint num_views = 0;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (view != NULL, 0);

  if (!gst_is_d3d11_buffer (buffer)) {
    GST_ERROR ("Buffer contains non-d3d11 memory");
    return 0;
  }

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    guint view_size;
    gint j;

    view_size = gst_d3d11_memory_get_render_target_view_size (mem);
    if (!view_size) {
      GST_LOG ("RTV is unavailable for memory index %d", i);
      return 0;
    }

    for (j = 0; j < view_size; j++) {
      if (num_views >= GST_VIDEO_MAX_PLANES) {
        GST_ERROR ("Too many RTVs");
        return 0;
      }

      view[num_views++] = gst_d3d11_memory_get_render_target_view (mem, j);
    }
  }

  return num_views;
}

GstBufferPool *
gst_d3d11_buffer_pool_new_with_options (GstD3D11Device * device,
    GstCaps * caps, GstD3D11AllocationParams * alloc_params,
    guint min_buffers, guint max_buffers)
{
  GstBufferPool *pool;
  GstStructure *config;
  GstVideoInfo info;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (GST_IS_CAPS (caps), NULL);
  g_return_val_if_fail (alloc_params != NULL, NULL);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (device, "invalid caps");
    return NULL;
  }

  pool = gst_d3d11_buffer_pool_new (device);
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config,
      caps, GST_VIDEO_INFO_SIZE (&info), min_buffers, max_buffers);

  gst_buffer_pool_config_set_d3d11_allocation_params (config, alloc_params);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (pool, "Couldn't set config");
    gst_object_unref (pool);
    return NULL;
  }

  return pool;
}