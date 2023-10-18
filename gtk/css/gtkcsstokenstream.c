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

#include "gtkcsstokenstreamprivate.h"

#include "gtk/gtkcsscustompropertypoolprivate.h" // TODO move this file to gtk/

void
gtk_css_token_stream_token_clear (GtkCssTokenStreamToken *self)
{
  if (self->is_reference)
    {
      GtkCssCustomPropertyPool *pool = gtk_css_custom_property_pool_get ();
      gtk_css_custom_property_pool_unref (pool, self->reference.id);
    }
  else
    {
      gtk_css_token_clear (&self->token.token);
      if (self->token.section)
        gtk_css_section_unref (self->token.section);
    }

  memset (self, 0, sizeof (GtkCssTokenStreamToken));
}

GtkCssTokenStream *
gtk_css_token_stream_new (GtkCssTokenStreamToken *tokens,
                          guint                   n_tokens)
{
  GtkCssTokenStream *self = g_new0 (GtkCssTokenStream, 1);

  self->ref_count = 1;
  self->tokens = tokens;
  self->n_tokens = n_tokens;

  return self;
}

GtkCssTokenStream *
gtk_css_token_stream_ref (GtkCssTokenStream *self)
{
  self->ref_count++;

  return self;
}

void
gtk_css_token_stream_unref (GtkCssTokenStream *self)
{
  guint i;

  self->ref_count--;
  if (self->ref_count > 0)
    return;

  for (i = 0; i < self->n_tokens; i++)
    {
      GtkCssTokenStreamToken *token = &self->tokens[i];

      if (token->is_reference)
        {
          GtkCssCustomPropertyPool *pool = gtk_css_custom_property_pool_get ();
          gtk_css_custom_property_pool_unref (pool, token->reference.id);
        }
      else
        {
          gtk_css_token_clear (&token->token.token);
          if (token->token.section)
            gtk_css_section_unref (token->token.section);
        }
    }

  g_free (self->tokens);
  g_free (self);
}

void
gtk_css_token_stream_print (GtkCssTokenStream *self,
                            GString           *string)
{
  guint i;

  for (i = 0; i < self->n_tokens; i++)
    {
      GtkCssTokenStreamToken *token = &self->tokens[i];

      if (i > 0)
        g_string_append_c (string, ' ');

      if (token->is_reference)
        {
          GtkCssCustomPropertyPool *pool = gtk_css_custom_property_pool_get ();
          const char *name = gtk_css_custom_property_pool_get_name (pool, token->reference.id);

          g_string_append (string, "var(");
          g_string_append (string, name);
          if (token->reference.fallback)
            {
              g_string_append (string, ", ");
              gtk_css_token_stream_print (token->reference.fallback, string);
            }
          g_string_append_c (string, ')');
        }
      else
        {
          gtk_css_token_print (&token->token.token, string);
        }
    }
}

