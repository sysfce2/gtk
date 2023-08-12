#pragma once

#include <gdk/gdk.h>
#include <gsk/gskrendernode.h>

#include "gskvulkanbufferprivate.h"
#include "gskvulkanprivate.h"
#include "gskvulkanrenderprivate.h"

G_BEGIN_DECLS


GskVkOldRenderPass *   gsk_vk_old_render_pass_new                      (void);

void                    gsk_vk_old_render_pass_free                     (GskVkOldRenderPass    *self);

void                    gsk_vk_old_render_pass_add                      (GskVkOldRenderPass    *self,
                                                                         GskVkOldRender        *render,
                                                                         int                     width,
                                                                         int                     height,
                                                                         cairo_rectangle_int_t  *clip,
                                                                         GskRenderNode          *node,
                                                                         const graphene_rect_t  *viewport);

G_END_DECLS

