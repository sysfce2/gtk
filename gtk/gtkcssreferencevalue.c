/* GTK - The GIMP Toolkit
 * Copyright (C) 2023 Alice Mikhaylenko <alicem@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gtkcssreferencevalueprivate.h"

#include "gtkcssarrayvalueprivate.h"
#include "gtkcssshorthandpropertyprivate.h"
#include "gtkcssstyleprivate.h"
#include "gtkcssunsetvalueprivate.h"
#include "gtkcssvalueprivate.h"

struct _GtkCssValue {
  GTK_CSS_VALUE_BASE

  GtkStyleProperty *property;
  GtkCssTokenStream *stream;
  GFile *file;
  guint subproperty;
};

static void
gtk_css_value_reference_free (GtkCssValue *value)
{
  gtk_css_token_stream_unref (value->stream);
  if (value->file)
    g_object_unref (value->file);
}
/*
static void
gtk_css_value_reference_parser_error (GtkCssParser         *parser,
                                      const GtkCssLocation *start,
                                      const GtkCssLocation *end,
                                      const GError         *error,
                                      gpointer              user_data)
{
  GtkCssValue *value = user_data;

//  gtk_css_parser_error_value (parser, "%s", error->message);
}
*/

static GtkCssTokenStream *
resolve_reference (GtkCssParser *parser,
                   int           id,
                   GtkCssStyle  *style)
{
  GtkCssTokenStream *value = gtk_css_style_get_custom_property (style, id);

  if (value == NULL)
    return NULL;

  return gtk_css_token_stream_ref (value);
}

static GtkCssValue *
gtk_css_value_reference_compute (GtkCssValue      *value,
                                 guint             property_id,
                                 GtkStyleProvider *provider,
                                 GtkCssStyle      *style,
                                 GtkCssStyle      *parent_style)
{
  GtkCssValue *result, *computed;
  GtkCssParser *value_parser =
    gtk_css_parser_new_for_token_stream (value->stream,
                                         value->file,
                                         (GtkCssParserResolveFunc) resolve_reference,
                                         NULL, style, NULL);//gtk_css_scanner_parser_error,
//                                       scanner, NULL);
  // TODO: cache parser
  // TODO: report errors

  result = _gtk_style_property_parse_value (value->property, value_parser);
  gtk_css_parser_unref (value_parser);

  if (result == NULL)
    result = _gtk_css_unset_value_new ();

  if (GTK_IS_CSS_SHORTHAND_PROPERTY (value->property))
    {
      // TODO handle initial/inherit/unset?
      GtkCssValue *sub = gtk_css_value_ref (_gtk_css_array_value_get_nth (result, value->subproperty));
      gtk_css_value_unref (result);
      result = sub;
    }

  computed = _gtk_css_value_compute (result,
                                     property_id,
                                     provider,
                                     style,
                                     parent_style);

  gtk_css_value_unref (result);

  return computed;
}

static gboolean
gtk_css_value_reference_equal (const GtkCssValue *value1,
                               const GtkCssValue *value2)
{
  return FALSE;
}

static GtkCssValue *
gtk_css_value_reference_transition (GtkCssValue *start,
                                    GtkCssValue *end,
                                    guint        property_id,
                                    double       progress)
{
  return NULL;
}

static void
gtk_css_value_reference_print (const GtkCssValue *value,
                               GString           *string)
{
  gtk_css_token_stream_print (value->stream, string);
}

static const GtkCssValueClass GTK_CSS_VALUE_REFERENCE = {
  "GtkCssReferenceValue",
  gtk_css_value_reference_free,
  gtk_css_value_reference_compute,
  gtk_css_value_reference_equal,
  gtk_css_value_reference_transition,
  NULL,
  NULL,
  gtk_css_value_reference_print
};

GtkCssValue *
_gtk_css_reference_value_new (GtkStyleProperty  *property,
                              GtkCssTokenStream *stream,
                              GFile             *file)
{
  GtkCssValue *result;

  result = _gtk_css_value_new (GtkCssValue, &GTK_CSS_VALUE_REFERENCE);
  result->property = property;
  result->stream = gtk_css_token_stream_ref (stream);

  if (file)
    result->file = g_object_ref (file);
  else
    result->file = NULL;

  return result;
}

void
_gtk_css_reference_value_set_subproperty (GtkCssValue *value,
                                          guint        property)
{
  g_assert (GTK_IS_CSS_SHORTHAND_PROPERTY (value->property));

  value->subproperty = property;
}
