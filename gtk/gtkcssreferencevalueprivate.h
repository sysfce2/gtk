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
#include "gtk/css/gtkcsstokenstreamprivate.h"
#include "gtkstylepropertyprivate.h"
#include "gtkcssvalueprivate.h"

G_BEGIN_DECLS

GtkCssValue *_gtk_css_reference_value_new             (GtkStyleProperty  *property,
                                                       GtkCssTokenStream *stream,
                                                       GFile             *file);

void         _gtk_css_reference_value_set_subproperty (GtkCssValue       *value,
                                                       guint              property);

G_END_DECLS
