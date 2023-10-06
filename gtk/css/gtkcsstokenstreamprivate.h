/*
 * Copyright © 2023 Alice Mikhaylenko <alicem@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <gtk/css/gtkcss.h>
#include "gtk/css/gtkcsstokenizerprivate.h"

G_BEGIN_DECLS

typedef struct _GtkCssTokenStream GtkCssTokenStream;
typedef struct _GtkCssTokenStreamToken GtkCssTokenStreamToken;

struct _GtkCssTokenStreamToken {
  gboolean is_reference;
  union {
    struct {
      GtkCssToken token;
      GtkCssSection *section;
    } token;
    struct {
      char *name;
      GtkCssTokenStream *fallback;
    } reference;
  };
};

struct _GtkCssTokenStream
{
  int ref_count;
  GtkCssTokenStreamToken *tokens;
  guint n_tokens;
};

void               gtk_css_token_stream_token_clear (GtkCssTokenStreamToken *token);

GtkCssTokenStream *gtk_css_token_stream_new         (GtkCssTokenStreamToken *tokens,
                                                     guint                   n_tokens);
GtkCssTokenStream *gtk_css_token_stream_ref         (GtkCssTokenStream      *self);
void               gtk_css_token_stream_unref       (GtkCssTokenStream      *self);

void               gtk_css_token_stream_print       (GtkCssTokenStream      *self,
                                                     GString                *string);

G_END_DECLS
