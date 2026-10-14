// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * kladde's XML schema: Inkscape's XML tree as durable values.
 *
 * A document is an element without a name, whose attributes are the XML
 * declaration's and whose children are the root element and the comments and
 * processing instructions around it. Element names and attribute names are
 * Inkscape's qualified names (`svg:rect`, `inkscape:label`), so a document
 * reads back exactly as Inkscape held it.
 */
#ifndef INKSCAPE_KLADDE_SCHEMA_H
#define INKSCAPE_KLADDE_SCHEMA_H

#include <string>
#include <variant>

#include <kladde/kladde.hpp>

namespace Inkscape::Kladde {

struct Attribute
{
    std::string name;
    std::string value;
    friend bool operator==(Attribute const &, Attribute const &) = default;
};
KLADDE_STRUCT(Attribute, name, value)

struct Text
{
    std::string content;
    bool cdata = false;
    friend bool operator==(Text const &, Text const &) = default;
};
KLADDE_STRUCT(Text, content, cdata)

struct Comment
{
    std::string content;
    friend bool operator==(Comment const &, Comment const &) = default;
};
KLADDE_STRUCT(Comment, content)

struct ProcessingInstruction
{
    std::string target;
    std::string content;
    friend bool operator==(ProcessingInstruction const &, ProcessingInstruction const &) = default;
};
KLADDE_STRUCT(ProcessingInstruction, target, content)

struct Element;

/// A node: an element, boxed since an element holds nodes, or a leaf.
using Node = std::variant<kladde::Box<Element>, Text, Comment, ProcessingInstruction>;

struct Element
{
    std::string name;
    /// In their order, as Inkscape keeps them.
    kladde::Vector<Attribute> attributes{};
    /// A list rather than a vector, since Inkscape holds pointers to nodes
    /// for as long as it likes, and each needs a `kladde::Ref`.
    kladde::List<Node> children{};
    friend bool operator==(Element const &, Element const &) = default;
};
KLADDE_STRUCT(Element, name, attributes, children)

} // namespace Inkscape::Kladde

KLADDE_VARIANT(Inkscape::Kladde::Node, "Node", 0, 1, 2, 3)

#endif // INKSCAPE_KLADDE_SCHEMA_H
