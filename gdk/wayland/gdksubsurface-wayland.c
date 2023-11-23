/*
 * Copyright © 2023 Red Hat, Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gdksubsurface-wayland-private.h"

#include "gdkmemoryformatprivate.h"
#include "gdkdisplay-wayland.h"
#include "gdkprivate-wayland.h"
#include "gdkdmabuftextureprivate.h"
#include "gdksurface-wayland-private.h"
#include "gdksubsurfaceprivate.h"
#include "gdkprivate.h"

#include "linux-dmabuf-unstable-v1-client-protocol.h"

enum {
  DMABUF_FORMATS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GdkWaylandSubsurface, gdk_wayland_subsurface, GDK_TYPE_SUBSURFACE)

static void
gdk_wayland_subsurface_init (GdkWaylandSubsurface *self)
{
}

static void
gdk_wayland_subsurface_finalize (GObject *object)
{
  GdkWaylandSubsurface *self = GDK_WAYLAND_SUBSURFACE (object);

  g_clear_object (&self->texture);
  g_clear_pointer (&self->frame_callback, wl_callback_destroy);
  g_clear_pointer (&self->opaque_region, wl_region_destroy);
  g_clear_pointer (&self->viewport, wp_viewport_destroy);
  g_clear_pointer (&self->subsurface, wl_subsurface_destroy);
  g_clear_pointer (&self->surface, wl_surface_destroy);
  g_clear_pointer (&self->subsurface, wl_subsurface_destroy);
  g_clear_pointer (&self->formats, dmabuf_formats_info_free);

  G_OBJECT_CLASS (gdk_wayland_subsurface_parent_class)->finalize (object);
}

static void
dmabuf_buffer_release (void             *data,
                       struct wl_buffer *buffer)
{
  GdkTexture *texture = data;

  g_object_unref (texture);
  wl_buffer_destroy (buffer);
}

static const struct wl_buffer_listener dmabuf_buffer_listener = {
  dmabuf_buffer_release,
};

typedef struct {
  struct wl_buffer *buffer;
  gboolean done;
} CreateBufferData;

static void
params_buffer_created (void                              *data,
                       struct zwp_linux_buffer_params_v1 *params,
                       struct wl_buffer                  *buffer)
{
  CreateBufferData *cd = data;

  cd->buffer = buffer;
  cd->done = TRUE;
}

static void
params_buffer_failed (void                              *data,
                      struct zwp_linux_buffer_params_v1 *params)
{
  CreateBufferData *cd = data;

  cd->buffer = NULL;
  cd->done = TRUE;
}

static const struct zwp_linux_buffer_params_v1_listener params_listener = {
  params_buffer_created,
  params_buffer_failed,
};

static struct wl_buffer *
get_wl_buffer (GdkWaylandSubsurface *self,
               GdkTexture           *texture)
{
  GdkWaylandDisplay *display = GDK_WAYLAND_DISPLAY (gdk_surface_get_display (GDK_SUBSURFACE (self)->parent));
  const GdkDmabuf *dmabuf;
  struct zwp_linux_buffer_params_v1 *params;
  struct wl_buffer *buffer;
  CreateBufferData cd = { NULL, FALSE };
  struct wl_event_queue *event_queue;

  dmabuf = gdk_dmabuf_texture_get_dmabuf (GDK_DMABUF_TEXTURE (texture));

  params = zwp_linux_dmabuf_v1_create_params (display->linux_dmabuf);

  for (gsize i = 0; i < dmabuf->n_planes; i++)
    zwp_linux_buffer_params_v1_add (params,
                                    dmabuf->planes[i].fd,
                                    i,
                                    dmabuf->planes[i].offset,
                                    dmabuf->planes[i].stride,
                                    dmabuf->modifier >> 32,
                                    dmabuf->modifier & 0xffffffff);

  event_queue = wl_display_create_queue (display->wl_display);

  wl_proxy_set_queue ((struct wl_proxy *) params, event_queue);

  zwp_linux_buffer_params_v1_add_listener (params, &params_listener, &cd);

  zwp_linux_buffer_params_v1_create (params,
                                     gdk_texture_get_width (texture),
                                     gdk_texture_get_height (texture),
                                     dmabuf->fourcc,
                                     0);

  while (!cd.done)
    gdk_wayland_display_dispatch_queue (GDK_DISPLAY (display), event_queue);

  wl_event_queue_destroy (event_queue);
  zwp_linux_buffer_params_v1_destroy (params);

  buffer = cd.buffer;

  if (buffer)
    {
      wl_proxy_set_queue ((struct wl_proxy *) buffer, NULL);
      wl_buffer_add_listener (buffer, &dmabuf_buffer_listener, g_object_ref (texture));
    }

  return buffer;
}

static gboolean
gdk_wayland_subsurface_attach (GdkSubsurface         *sub,
                               GdkTexture            *texture,
                               const graphene_rect_t *rect,
                               gboolean               above,
                               GdkSubsurface         *sibling)
{
  GdkWaylandSubsurface *self = GDK_WAYLAND_SUBSURFACE (sub);
  GdkWaylandSurface *parent = GDK_WAYLAND_SURFACE (sub->parent);
  struct wl_buffer *buffer = NULL;
  gboolean result = FALSE;
  GdkWaylandSubsurface *sib = sibling ? GDK_WAYLAND_SUBSURFACE (sibling) : NULL;
  gboolean will_be_above;
  double scale;
  graphene_rect_t device_rect;
  cairo_rectangle_int_t device_dest;

  if (sib)
    will_be_above = sib->above_parent;
  else
    will_be_above = above;

  if (sub->parent == NULL)
    {
      g_warning ("Can't attach to destroyed subsurface %p", self);
      return FALSE;
    }

  self->dest.x = rect->origin.x;
  self->dest.y = rect->origin.y;
  self->dest.width = rect->size.width;
  self->dest.height = rect->size.height;

  scale = gdk_fractional_scale_to_double (&parent->scale);
  device_rect.origin.x = rect->origin.x * scale;
  device_rect.origin.y = rect->origin.y * scale;
  device_rect.size.width = rect->size.width * scale;
  device_rect.size.height = rect->size.height * scale;
  device_dest.x = device_rect.origin.x;
  device_dest.y = device_rect.origin.y;
  device_dest.width = device_rect.size.width;
  device_dest.height = device_rect.size.height;

  if (self->dest.x != rect->origin.x ||
      self->dest.y != rect->origin.y ||
      self->dest.width != rect->size.width ||
      self->dest.height != rect->size.height)
    {
      GDK_DISPLAY_DEBUG (gdk_surface_get_display (sub->parent), OFFLOAD,
                         "Non-integer coordinates %g %g %g %g for %dx%d texture, hiding subsurface %p",
                         rect->origin.x, rect->origin.y,
                         rect->size.width, rect->size.height,
                         gdk_texture_get_width (texture),
                         gdk_texture_get_height (texture),
                         self);
    }
  else if (device_dest.x != device_rect.origin.x ||
           device_dest.y != device_rect.origin.y ||
           device_dest.width != device_rect.size.width ||
           device_dest.height != device_rect.size.height)
    {
      GDK_DISPLAY_DEBUG (gdk_surface_get_display (sub->parent), OFFLOAD,
                         "Non-integral device coordinates %g %g %g %g (fractional scale %.2f), hiding subsurface %p",
                         device_rect.origin.x, device_rect.origin.y,
                         device_rect.size.width, device_rect.size.width,
                         scale,
                         self);
    }
  else if (!GDK_IS_DMABUF_TEXTURE (texture))
    {
      GDK_DISPLAY_DEBUG (gdk_surface_get_display (sub->parent), OFFLOAD,
                         "%dx%d %s is not a GdkDmabufTexture, hiding subsurface %p",
                         gdk_texture_get_width (texture),
                         gdk_texture_get_height (texture),
                         G_OBJECT_TYPE_NAME (texture),
                         self);
    }
  else if (!will_be_above &&
           gdk_memory_format_alpha (gdk_texture_get_format (texture)) != GDK_MEMORY_ALPHA_OPAQUE)
    {
      GDK_DISPLAY_DEBUG (gdk_surface_get_display (sub->parent), OFFLOAD,
                         "Cannot offload non-opaque %dx%d texture below, hiding subsurface %p",
                         gdk_texture_get_width (texture),
                         gdk_texture_get_height (texture),
                         self);
    }
  else
    {
      if (g_set_object (&self->texture, texture))
        {
          buffer = get_wl_buffer (self, texture);
          if (buffer != NULL)
            {
              GDK_DISPLAY_DEBUG (gdk_surface_get_display (sub->parent), OFFLOAD,
                                 "Attached %dx%d texture to subsurface %p at %d %d %d %d",
                                 gdk_texture_get_width (texture),
                                 gdk_texture_get_height (texture),
                                 self,
                                 self->dest.x, self->dest.y,
                                 self->dest.width, self->dest.height);
              result = TRUE;
            }
          else
            {
              GDK_DISPLAY_DEBUG (gdk_surface_get_display (sub->parent), OFFLOAD,
                                 "Compositor failed to create wl_buffer for %dx%d texture, hiding subsurface %p",
                                 gdk_texture_get_width (texture),
                                 gdk_texture_get_height (texture),
                                 self);
            }
        }
      else
        {
          buffer = NULL;
          GDK_DISPLAY_DEBUG (gdk_surface_get_display (sub->parent), OFFLOAD,
                             "Moved %dx%d texture in subsurface %p to %d %d %d %d",
                             gdk_texture_get_width (texture),
                             gdk_texture_get_height (texture),
                             self,
                             self->dest.x, self->dest.y,
                             self->dest.width, self->dest.height);
          result = TRUE;
        }
    }

  if (result)
    {
      wl_subsurface_set_position (self->subsurface, self->dest.x, self->dest.y);
      wp_viewport_set_destination (self->viewport, self->dest.width, self->dest.height);

      if (buffer)
        {
          wl_surface_attach (self->surface, buffer, 0, 0);
          wl_surface_damage_buffer (self->surface,
                                    0, 0,
                                    gdk_texture_get_width (texture),
                                    gdk_texture_get_height (texture));

        }

      result = TRUE;
    }
  else
    {
      g_set_object (&self->texture, NULL);

      wl_surface_attach (self->surface, NULL, 0, 0);
    }

  if (sib)
    {
      if (above)
        wl_subsurface_place_above (self->subsurface, sib->surface);
      else
        wl_subsurface_place_below (self->subsurface, sib->surface);

      self->above_parent = sib->above_parent;
    }
  else
    {
      if (above)
        wl_subsurface_place_above (self->subsurface,
                                   GDK_WAYLAND_SURFACE (sub->parent)->display_server.wl_surface);
      else
        wl_subsurface_place_below (self->subsurface,
                                   GDK_WAYLAND_SURFACE (sub->parent)->display_server.wl_surface);
      self->above_parent = above;
    }

  wl_surface_commit (self->surface);

  ((GdkWaylandSurface *)sub->parent)->has_pending_subsurface_commits = TRUE;
  GDK_WAYLAND_SURFACE (sub->parent)->opaque_region_dirty = TRUE;

  return result;
}

static void
gdk_wayland_subsurface_detach (GdkSubsurface *sub)
{
  GdkWaylandSubsurface *self = GDK_WAYLAND_SUBSURFACE (sub);

  if (sub->parent == NULL)
    {
      g_warning ("Can't draw to destroyed subsurface %p", self);
      return;
    }

  g_set_object (&self->texture, NULL);
  wl_surface_attach (self->surface, NULL, 0, 0);
  wl_surface_commit (self->surface);

  ((GdkWaylandSurface *)sub->parent)->has_pending_subsurface_commits = TRUE;
  GDK_WAYLAND_SURFACE (sub->parent)->opaque_region_dirty = TRUE;
}

static GdkTexture *
gdk_wayland_subsurface_get_texture (GdkSubsurface *sub)
{
  GdkWaylandSubsurface *self = GDK_WAYLAND_SUBSURFACE (sub);

  return self->texture;
}

static void
gdk_wayland_subsurface_get_rect (GdkSubsurface   *sub,
                                 graphene_rect_t *rect)
{
  GdkWaylandSubsurface *self = GDK_WAYLAND_SUBSURFACE (sub);

  rect->origin.x = self->dest.x;
  rect->origin.y = self->dest.y;
  rect->size.width = self->dest.width;
  rect->size.height = self->dest.height;
}

static gboolean
gdk_wayland_subsurface_is_above_parent (GdkSubsurface *sub)
{
  GdkWaylandSubsurface *self = (GdkWaylandSubsurface *)sub;

  return self->above_parent;
}

static void
gdk_wayland_subsurface_class_init (GdkWaylandSubsurfaceClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GdkSubsurfaceClass *subsurface_class = GDK_SUBSURFACE_CLASS (class);

  object_class->finalize = gdk_wayland_subsurface_finalize;

  subsurface_class->attach = gdk_wayland_subsurface_attach;
  subsurface_class->detach = gdk_wayland_subsurface_detach;
  subsurface_class->get_texture = gdk_wayland_subsurface_get_texture;
  subsurface_class->get_rect = gdk_wayland_subsurface_get_rect;
  subsurface_class->is_above_parent = gdk_wayland_subsurface_is_above_parent;

  signals[DMABUF_FORMATS_CHANGED] =
    g_signal_new (I_("dmabuf-formats-changed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  0);
};

static void
frame_callback (void               *data,
                struct wl_callback *callback,
                uint32_t            time)
{
  GdkSubsurface *sub = data;

  g_assert (((GdkWaylandSubsurface *)sub)->frame_callback == callback);
  g_assert (!GDK_SURFACE_DESTROYED (sub->parent));

  gdk_wayland_surface_frame_callback (sub->parent, time);
}

static const struct wl_callback_listener frame_listener = {
  frame_callback
};

void
gdk_wayland_subsurface_request_frame (GdkSubsurface *sub)
{
  GdkWaylandSubsurface *self = (GdkWaylandSubsurface *)sub;

  self->frame_callback = wl_surface_frame (self->surface);
  wl_proxy_set_queue ((struct wl_proxy *) self->frame_callback, NULL);
  wl_callback_add_listener (self->frame_callback, &frame_listener, self);
  wl_surface_commit (self->surface);
}

void
gdk_wayland_subsurface_clear_frame_callback (GdkSubsurface *sub)
{
  GdkWaylandSubsurface *self = (GdkWaylandSubsurface *)sub;

  g_clear_pointer (&self->frame_callback, wl_callback_destroy);
}

static void
dmabuf_formats_callback (gpointer           data,
                         DmabufFormatsInfo *info)
{
  GdkSubsurface *sub = data;

  gdk_subsurface_set_dmabuf_formats (sub, info->formats);
}

GdkSubsurface *
gdk_wayland_surface_create_subsurface (GdkSurface *surface)
{
  GdkWaylandSurface *impl = GDK_WAYLAND_SURFACE (surface);
  GdkDisplay *display = gdk_surface_get_display (surface);
  GdkWaylandDisplay *disp = GDK_WAYLAND_DISPLAY (display);
  GdkWaylandSubsurface *sub;
  struct wl_region *region;
  struct zwp_linux_dmabuf_feedback_v1 *feedback;
  char *name;

  if (disp->viewporter == NULL)
    {
      GDK_DISPLAY_DEBUG (display, OFFLOAD, "Can't use subsurfaces without viewporter");
      return NULL;
    }

  sub = g_object_new (GDK_TYPE_WAYLAND_SUBSURFACE, NULL);

  sub->surface = wl_compositor_create_surface (disp->compositor);
  sub->subsurface = wl_subcompositor_get_subsurface (disp->subcompositor,
                                                     sub->surface,
                                                     impl->display_server.wl_surface);
  sub->viewport = wp_viewporter_get_viewport (disp->viewporter, sub->surface);

  /* No input, please */
  region = wl_compositor_create_region (disp->compositor);
  wl_surface_set_input_region (sub->surface, region);
  wl_region_destroy (region);

  /* Keep a max-sized opaque region so we don't have to update it
   * when the size of the texture changes.
   */
  sub->opaque_region = wl_compositor_create_region (disp->compositor);
  wl_region_add (sub->opaque_region, 0, 0, G_MAXINT, G_MAXINT);
  wl_surface_set_opaque_region (sub->surface, sub->opaque_region);

  sub->above_parent = TRUE;

  gdk_display_init_dmabuf (display);

  if (disp->linux_dmabuf)
    feedback = zwp_linux_dmabuf_v1_get_surface_feedback (disp->linux_dmabuf, sub->surface);
  else
    feedback = NULL;

  name = g_strdup_printf ("subsurface %p", sub);
  sub->formats = dmabuf_formats_info_new (name,
                                          display->dmabuf_formats,
                                          feedback,
                                          dmabuf_formats_callback,
                                          sub);
  g_free (name);

  GDK_DISPLAY_DEBUG (display, OFFLOAD, "Subsurface %p of surface %p created", sub, impl);

  return GDK_SUBSURFACE (sub);
}

GdkDmabufFormats *
gdk_wayland_subsurface_get_formats (GdkSubsurface *subsurface)
{
  return NULL;
}

dev_t
gdk_wayland_subsurface_get_main_device (GdkSubsurface *subsurface)
{
  return 0;
}

dev_t
gdk_wayland_subsurface_get_target_device (GdkSubsurface *subsurface,
                                          guint32        fourcc,
                                          guint64        modifier)
{
  return 0;
}

gboolean
gdk_wayland_subsurface_get_allows_scanout (GdkSubsurface *subsurface,
                                           guint32        fourcc,
                                           guint64        modifier)
{
  return FALSE;
}
