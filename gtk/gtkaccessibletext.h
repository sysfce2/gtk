/* gtkaccessibletext.h: Interface for accessible objects containing text
 *
 * SPDX-FileCopyrightText: 2023  Emmanuele Bassi
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#if !defined (__GTK_H_INSIDE__) && !defined (GTK_COMPILATION)
#error "Only <gtk/gtk.h> can be included directly."
#endif

#include <gtk/gtkaccessible.h>

G_BEGIN_DECLS

#define GTK_TYPE_ACCESSIBLE_TEXT (gtk_accessible_text_get_type ())

GDK_AVAILABLE_IN_4_14
G_DECLARE_INTERFACE (GtkAccessibleText, gtk_accessible_text, GTK, ACCESSIBLE_TEXT, GtkAccessible)

/**
 * GtkAccessibleTextRange:
 * @start: the start of the range, in characters
 * @end: the end of the range, in characters
 *
 * A range inside the text of an accessible object.
 *
 * Since: 4.14
 */
typedef struct {
  int start;
  int end;
} GtkAccessibleTextRange;

/**
 * GtkAccessibleTextInterface:
 *
 * The interface vtable for accessible objects containing text.
 *
 * Since: 4.14
 */
struct _GtkAccessibleTextInterface
{
  /*< private >*/
  GTypeInterface g_iface;

  /*< public >*/

  /**
   * GtkAccessibleTextInterface::get_contents:
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
  GBytes * (* get_contents) (GtkAccessibleText *self,
                             unsigned int       start,
                             int                end);

  /**
   * GtkAccessibleTextInterface::get_caret_position:
   * @self: the accessible object
   *
   * Retrieves the position of the caret inside the accessible object.
   *
   * Returns: the position of the caret, in characters
   *
   * Since: 4.14
   */
  unsigned int (* get_caret_position) (GtkAccessibleText *self);

  /**
   * GtkAccessibleTextInterface::get_selection:
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
  gboolean (* get_selection) (GtkAccessibleText *self,
                              gsize *n_ranges,
                              GtkAccessibleTextRange *ranges);

  /**
   * GtkAccessibleTextInterface::get_attributes:
   * @self: the accessible object
   * @n_attributes: (out): the number of attributes
   * @ranges: (out) (array length=n_attributes) (optional): the ranges of the attributes
   *   inside the accessible object
   * @attribute_names: (out) (array length=n_attributes) (optional): the
   *   names of the attributes inside the accessible object
   * @attribute_values: (out) (array length=n_attributes) (optional): the
   *   values of the attributes inside the accessible object
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
  gboolean (* get_attributes) (GtkAccessibleText *self,
                               gsize *n_attributes,
                               GtkAccessibleTextRange *ranges,
                               char **attribute_names,
                               char **attribute_values);
};

G_END_DECLS
