// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Between Inkscape's attribute strings and the typed values of kladde's SVG
 * schema (see schema.h).
 *
 * An attribute is typed only if formatting its typed value reproduces its
 * text exactly; anything else is kept verbatim as `Other`. So
 * `attribute_value(attribute_of(name, value)) == value` for every attribute,
 * and a document reads back byte for byte as Inkscape held it.
 */
#ifndef INKSCAPE_KLADDE_SVG_VALUES_H
#define INKSCAPE_KLADDE_SVG_VALUES_H

#include <string>
#include <string_view>

#include "kladde-io/schema.h"

namespace Inkscape::Kladde {

/// The attribute `name="value"`: typed if the schema knows `name` and
/// `value` formats back exactly, `Other` otherwise.
Attr attribute_of(std::string_view name, std::string_view value);

/// The attribute's qualified name.
std::string_view attribute_name(Attr const &attr);

/// The attribute's value as text, exactly as it was given.
std::string attribute_value(Attr const &attr);

/// Whether the attribute holds a typed value rather than its text.
bool is_typed(Attr const &attr);

/// Whether the schema has a type for the attribute `name`, which its values
/// get where they format back exactly.
bool types_attribute(std::string_view name);

/// The element name for Inkscape's qualified name `name` (`svg:rect`).
Name name_of(std::string_view name);

/// Inkscape's qualified name for `name`.
std::string qualified_name(Name const &name);

} // namespace Inkscape::Kladde

#endif // INKSCAPE_KLADDE_SVG_VALUES_H
