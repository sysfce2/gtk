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

#include "gtkcss.h"
#include "gtkcsstokenizerprivate.h"

G_BEGIN_DECLS

typedef struct _GtkCssVariableValue GtkCssVariableValue;

struct _GtkCssVariableValue
{
  int ref_count;
  GtkCssSection *section;
  GtkCssToken *tokens;
  gsize n_tokens;
};

GtkCssVariableValue *gtk_css_variable_value_new   (GtkCssSection             *section,
                                                   GtkCssToken               *tokens,
                                                   gsize                      n_tokens);
GtkCssVariableValue *gtk_css_variable_value_ref   (GtkCssVariableValue       *self);
void                 gtk_css_variable_value_unref (GtkCssVariableValue       *self);
void                 gtk_css_variable_value_print (GtkCssVariableValue       *self,
                                                   GString                   *string);
gboolean             gtk_css_variable_value_equal (const GtkCssVariableValue *value1,
                                                   const GtkCssVariableValue *value2) G_GNUC_PURE;

G_END_DECLS
