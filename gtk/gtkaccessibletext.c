/* gtkaccessibletext.c: Interface for accessible text objects
 *
 * SPDX-FileCopyrightText: 2023  Emmanuele Bassi
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "gtkaccessibletext-private.h"

G_DEFINE_INTERFACE (GtkAccessibleText, gtk_accessible_text, GTK_TYPE_ACCESSIBLE)

static GBytes *
gtk_accessible_text_default_get_contents (GtkAccessibleText *self,
                                          unsigned int start,
                                          int end)
{
  return NULL;
}

static unsigned int
gtk_accessible_text_default_get_caret_position (GtkAccessibleText *self)
{
  return 0;
}

static gboolean
gtk_accessible_text_default_get_selection (GtkAccessibleText      *self,
                                           gsize                  *n_ranges,
                                           GtkAccessibleTextRange *ranges)
{
  return FALSE;
}

static gboolean
gtk_accessible_text_default_get_attributes (GtkAccessibleText       *self,
                                            gsize                   *n_attributes,
                                            GtkAccessibleTextRange  *ranges,
                                            char                   **attribute_names,
                                            char                   **attribute_values)
{
  return FALSE;
}

static void
gtk_accessible_text_default_init (GtkAccessibleTextInterface *iface)
{
  iface->get_contents = gtk_accessible_text_default_get_contents;
  iface->get_caret_position = gtk_accessible_text_default_get_caret_position;
  iface->get_selection = gtk_accessible_text_default_get_selection;
  iface->get_attributes = gtk_accessible_text_default_get_attributes;
}

/*< private >
 * gtk_accessible_text_get_contents:
 * @self: the accessible object
 * @start: the beginning of the range, in characters
 * @end: the end of the range, in characters
 *
 * Retrieve the current contents of the accessible object within
 * the given range.
 *
 * If @end is -1, the end of the range is the full content of the
 * accessible object.
 *
 * Returns: (transfer full): the contents of the accessible object
 *
 * Since: 4.14
 */
GBytes *
gtk_accessible_text_get_contents (GtkAccessibleText *self,
                                  unsigned int       start,
                                  int                end)
{
  g_return_val_if_fail (GTK_IS_ACCESSIBLE_TEXT (self), NULL);
  g_return_val_if_fail (end == -1 || end >= start, NULL);

  return GTK_ACCESSIBLE_TEXT_GET_IFACE (self)->get_contents (self, start, end);
}

/*< private >
 * gtk_accessible_text_get_caret_position:
 * @self: the accessible object
 *
 * Retrieves the position of the caret inside the accessible object.
 *
 * Returns: the position of the caret, in characters
 *
 * Since: 4.14
 */
unsigned int
gtk_accessible_text_get_caret_position (GtkAccessibleText *self)
{
  g_return_val_if_fail (GTK_IS_ACCESSIBLE_TEXT (self), 0);

  return GTK_ACCESSIBLE_TEXT_GET_IFACE (self)->get_caret_position (self);
}

/*< private >
 * gtk_accessible_text_get_selection:
 * @self: the accessible object
 * @n_ranges: (out): the number of selection ranges
 * @ranges: (optional) (out) (array length=n_ranges): the selection ranges
 *
 * Retrieves the selection ranges in the accessible object.
 *
 * If this function returns true, `n_ranges` will be set to a value
 * greater than or equal to one.
 *
 * Returns: true if there is at least a selection inside the
 *   accessible object, and false otherwise
 *
 * Since: 4.14
 */
gboolean
gtk_accessible_text_get_selection (GtkAccessibleText      *self,
                                   gsize                  *n_ranges,
                                   GtkAccessibleTextRange *ranges)
{
  g_return_val_if_fail (GTK_IS_ACCESSIBLE_TEXT (self), FALSE);

  return GTK_ACCESSIBLE_TEXT_GET_IFACE (self)->get_selection (self, n_ranges, ranges);
}

/*< private >
 * gtk_accessible_text_get_attributes:
 * @self: the accessible object
 * @n_attributes: (out): the number of attributes
 * @ranges: (out) (array length=n_attributes) (optional): the ranges of the attributes
 *   inside the accessible object
 * @attribute_names: (out) (array length=n_attributes) (element-type utf8) (optional) (transfer full):
 *   the names of the attributes inside the accessible object
 * @attribute_values: (out) (array length=n_attributes) (element-type utf8) (optional) (transfer full):
 *   the values of the attributes inside the accessible object
 *
 * Retrieves the text attributes inside the accessible object.
 *
 * Each attribute is composed by:
 *
 * - a range
 * - a name, typically in the form of a reverse DNS identifier
 * - a value
 *
 * If this function returns true, `n_attributes` will be set to a value
 * greater than or equal to one.
 *
 * Returns: true if the accessible object has at least an attribute,
 *   and false otherwise
 *
 * Since: 4.14
 */
gboolean
gtk_accessible_text_get_attributes (GtkAccessibleText       *self,
                                    gsize                   *n_attributes,
                                    GtkAccessibleTextRange  *ranges,
                                    char                   **attribute_names,
                                    char                   **attribute_values)
{
  g_return_val_if_fail (GTK_IS_ACCESSIBLE_TEXT (self), FALSE);

  return GTK_ACCESSIBLE_TEXT_GET_IFACE (self)->get_attributes (self,
                                                               n_attributes,
                                                               ranges,
                                                               attribute_names,
                                                               attribute_values);
}
