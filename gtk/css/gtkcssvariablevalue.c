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

#include "gtkcssvariablevalueprivate.h"

GtkCssVariableValue *
gtk_css_variable_value_new (GtkCssSection *section,
                            GtkCssToken   *tokens,
                            gsize          n_tokens)
{
  GtkCssVariableValue *self = g_new0 (GtkCssVariableValue, 1);

  self->ref_count = 1;
  self->section = section;
  self->tokens = tokens;
  self->n_tokens = n_tokens;

  return self;
}

GtkCssVariableValue *
gtk_css_variable_value_ref (GtkCssVariableValue *self)
{
  self->ref_count++;

  return self;
}

void
gtk_css_variable_value_unref (GtkCssVariableValue *self)
{
  self->ref_count--;
  if (self->ref_count > 0)
    return;

  if (self->section)
    gtk_css_section_unref (self->section);
  g_free (self->tokens);
  g_free (self);
}

void
gtk_css_variable_value_print (GtkCssVariableValue *self,
                              GString             *string)
{
  gsize i;

  for (i = 0; i < self->n_tokens; i++)
    {
      if (i > 0)
        g_string_append_c (string, ' ');

      gtk_css_token_print (&self->tokens[i], string);
    }
}

gboolean
gtk_css_variable_value_equal (const GtkCssVariableValue *value1,
                              const GtkCssVariableValue *value2)
{
  gsize i;

  if (value1 == value2)
    return TRUE;

  if (value1 == NULL && value2 == NULL)
    return TRUE;

  if (value1 == NULL || value2 == NULL)
    return FALSE;

  if (value1->n_tokens != value2->n_tokens)
    return FALSE;

  for (i = 0; i < value1->n_tokens; i++)
    {
      if (!gtk_css_token_equal (&value1->tokens[i], &value2->tokens[i]))
        return FALSE;
    }

  return TRUE;
}
