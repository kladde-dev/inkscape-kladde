// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A kladde file that backs a document: every change to the document's XML
 * tree is recorded in the file as it happens, so there is nothing to save.
 *
 * The file holds the XML tree in kladde's typed SVG schema (see schema.h),
 * whose attributes are typed values wherever they format back exactly. A
 * subtree observer on the document node mirrors every change Inkscape makes
 * to its tree — attribute sets, content changes, insertions, removals and
 * reorderings — into the file, each as one or two records: an attribute set
 * parses the new value and records a splice of the changed middle of the
 * attribute's encoding, a reorder is a `Move`.
 * Long-lived `kladde::Ref`s map Inkscape's nodes to their durable
 * counterparts, so a change deep in the tree finds its place in the file in
 * O(depth · log m).
 */
#ifndef INKSCAPE_KLADDE_BACKING_H
#define INKSCAPE_KLADDE_BACKING_H

#include <memory>
#include <string>
#include <unordered_map>

#include <sigc++/connection.h>

#include "kladde-io/schema.h"
#include "xml/node-observer.h"

class SPDocument;

namespace Inkscape::XML {
struct Document;
class Node;
} // namespace Inkscape::XML

namespace Inkscape::Kladde {

/// The file extension of kladde files.
inline constexpr char const *extension = ".kladde";

/// Whether `path` names a kladde file, by its extension.
bool is_kladde_path(std::string const &path);

class Backing final : public XML::NodeObserver
{
public:
    /// Creates (or replaces) the kladde file at `path` holding `doc`'s XML
    /// tree, and backs `doc` with it from now on. Returns null, after telling
    /// the user, if the file cannot be created.
    static Backing *create(SPDocument &doc, std::string const &path);

    /// Opens the kladde file at `path` and builds a document from it, backed
    /// by the file. Returns null, after printing why, if it cannot be opened.
    static std::unique_ptr<SPDocument> open(std::string const &path);

    ~Backing() override;

    std::string const &path() const { return _path; }

    /// Folds the journal into the file now.
    void flush();

    /// The store's counters, for measurements.
    kladde::Stats stats() const;

    /// The size of the document's encoding in the file, for measurements.
    std::size_t encoded_size() const;

    /// The document as the file holds it, for measurements.
    Element const &root() const;

    void notifyChildAdded(XML::Node &node, XML::Node &child, XML::Node *prev) override;
    void notifyChildRemoved(XML::Node &node, XML::Node &child, XML::Node *prev) override;
    void notifyChildOrderChanged(XML::Node &node, XML::Node &child, XML::Node *old_prev,
                                 XML::Node *new_prev) override;
    void notifyContentChanged(XML::Node &node, Util::ptr_shared old_content, Util::ptr_shared new_content) override;
    void notifyAttributeChanged(XML::Node &node, GQuark name, Util::ptr_shared old_value,
                                Util::ptr_shared new_value) override;
    void notifyElementNameChanged(XML::Node &node, GQuark old_name, GQuark new_name) override;

private:
    struct Impl;
    Backing(std::string path, std::unique_ptr<Impl> impl);
    void attach(SPDocument &doc);

    std::string _path;
    std::unique_ptr<Impl> _impl;
};

} // namespace Inkscape::Kladde

#endif // INKSCAPE_KLADDE_BACKING_H
