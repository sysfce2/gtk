#pragma once

#include "gskvulkanopprivate.h"

G_BEGIN_DECLS

void                    gsk_vk_old_download_op                          (GskVkOldRender                *render,
                                                                         GskVkOldImage                 *image,
                                                                         GskVkOldDownloadFunc           func,
                                                                         gpointer                        user_data);

void                    gsk_vk_old_download_png_op                      (GskVkOldRender                *render,
                                                                         GskVkOldImage                 *image,
                                                                         const char                     *filename_format,
                                                                         ...) G_GNUC_PRINTF(3, 4);

G_END_DECLS

