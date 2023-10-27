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
#include "gtkcsscustompropertypoolprivate.h"
#include "gtkcssshorthandpropertyprivate.h"
#include "gtkcssstyleprivate.h"
#include "gtkcssunsetvalueprivate.h"
#include "gtkcssvalueprivate.h"

#define MAX_TOKEN_LENGTH 65536

struct _GtkCssValue {
  GTK_CSS_VALUE_BASE

  GtkStyleProperty *property;
  GtkCssVariableValue *value;
  GFile *file;
  guint subproperty;
};

static void
gtk_css_value_reference_free (GtkCssValue *value)
{
  gtk_css_variable_value_unref (value->value);
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

static gboolean
resolve_references_do (GtkCssToken *tokens,
                       gsize        n_tokens,
                       gboolean     limit_length,
                       GtkCssStyle *style,
                       GArray      *output)
{
  GtkCssCustomPropertyPool *pool = gtk_css_custom_property_pool_get ();
  GArray *blocks;
  gsize i;
  int var_id = -1;
  int var_level = -1;
  int fallback_start = -1;

  blocks = g_array_new (FALSE, FALSE, sizeof (GtkCssTokenType));

  for (i = 0; i < n_tokens; i++)
    {
      GtkCssToken *token = &tokens[i];
      GtkCssTokenType closing_type;

      if (!gtk_css_token_is_preserved (token, &closing_type))
        {
          g_array_append_val (blocks, closing_type);
          if (var_level < 0 && gtk_css_token_is_function (token, "var"))
            {
              const char *name;

              var_level = blocks->len;

              if (i + 1 >= n_tokens ||
                  !gtk_css_token_is (&tokens[i + 1], GTK_CSS_TOKEN_IDENT))
                {
                  goto error;
                }

              name = gtk_css_token_get_string (&tokens[i + 1]);
              var_id = gtk_css_custom_property_pool_add (pool, name);

              if (i + 2 >= n_tokens)
                goto error;

              if (gtk_css_token_is (&tokens[i + 2], GTK_CSS_TOKEN_COMMA))
                {
                  i += 3;

                  if (i >= n_tokens)
                    goto error;

                  fallback_start = i;
                }
              else
                i++;

              continue;
            }
        }
      else if (blocks->len > 0 && token->type == g_array_index (blocks, GtkCssTokenType, blocks->len - 1))
        {
          if (var_level == blocks->len)
            {
              GtkCssVariableValue *value = gtk_css_style_get_custom_property (style, var_id);
              gboolean success = FALSE;

              if (value != NULL && value->n_tokens > 0)
                {
                  GArray *buffer = g_array_new (FALSE, FALSE, sizeof (GtkCssToken));

                  success |= resolve_references_do (value->tokens, value->n_tokens,
                                                    TRUE, style, buffer);

                  if (success)
                    g_array_append_vals (output, buffer->data, buffer->len);

                  g_array_unref (buffer);
                }

              if (!success)
                {
                  if (fallback_start >= 0)
                    {
                      resolve_references_do (&tokens[fallback_start], i - fallback_start,
                                             FALSE, style, output);
                    }
                  else
                    goto error;
                }

              if (limit_length && output->len > MAX_TOKEN_LENGTH)
                goto error;

              var_id = -1;
              fallback_start = -1;
              var_level = -1;
              continue;
            }
          else
            g_array_remove_index_fast (blocks, blocks->len - 1);
        }

      if (var_level < 0)
        {
          g_array_append_val (output, tokens[i]);

          if (limit_length && output->len > MAX_TOKEN_LENGTH)
            goto error;
        }
    }

  g_array_unref (blocks);

  return TRUE;

error:
  g_array_unref (blocks);

  return FALSE;
}

static GtkCssVariableValue *
resolve_references (GtkCssVariableValue *input,
                    GtkCssStyle         *style)
{
  GArray *tokens = g_array_new (FALSE, FALSE, sizeof (GtkCssToken));
  GtkCssToken *ret_tokens;
  gsize n_tokens;

  if (!resolve_references_do (input->tokens, input->n_tokens, FALSE, style, tokens))
    return NULL;

  ret_tokens = g_array_steal (tokens, &n_tokens);

  return gtk_css_variable_value_new (input->section, ret_tokens, n_tokens);
}

static GtkCssValue *
gtk_css_value_reference_compute (GtkCssValue      *value,
                                 guint             property_id,
                                 GtkStyleProvider *provider,
                                 GtkCssStyle      *style,
                                 GtkCssStyle      *parent_style)
{
  GtkCssVariableValue *var_value;
  GtkCssValue *result = NULL, *computed;

  var_value = resolve_references (value->value, style);

  if (var_value != NULL)
    {
      GtkCssParser *value_parser =
        gtk_css_parser_new_for_token_stream (var_value,
                                             value->file,
                                             NULL, style, NULL);//gtk_css_scanner_parser_error,
    //                                       scanner, NULL);
      // TODO: cache parser
      // TODO: report errors

      result = _gtk_style_property_parse_value (value->property, value_parser);
      gtk_css_parser_unref (value_parser);
      gtk_css_variable_value_unref (var_value);
    }

  if (result == NULL)
    result = _gtk_css_unset_value_new ();

  if (GTK_IS_CSS_SHORTHAND_PROPERTY (value->property))
    {
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
  gtk_css_variable_value_print (value->value, string);
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
_gtk_css_reference_value_new (GtkStyleProperty    *property,
                              GtkCssVariableValue *value,
                              GFile               *file)
{
  GtkCssValue *result;

  result = _gtk_css_value_new (GtkCssValue, &GTK_CSS_VALUE_REFERENCE);
  result->property = property;
  result->value = gtk_css_variable_value_ref (value);

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
