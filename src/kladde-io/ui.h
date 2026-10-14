// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * The user-facing side of kladde-backed documents: asking where a new
 * document's kladde file goes, and importing a file of another format into a
 * new kladde file next to it.
 */
#ifndef INKSCAPE_KLADDE_UI_H
#define INKSCAPE_KLADDE_UI_H

#include <memory>
#include <string>
#include <utility>

class SPDocument;
namespace Gtk {
class Window;
}

namespace Inkscape::Kladde {

/// Asks where to create the kladde file of a new document; empty if the
/// user cancelled. The path ends in `.kladde`.
std::string ask_new_path(Gtk::Window *parent);

/// The kladde file that importing `source` creates: `source` with its
/// extension replaced by `.kladde`.
std::string import_path(std::string const &source);

/// Backs `doc`, just opened from `source`, a file of another format, by a new
/// kladde file next to it, which records every change from now on; `source`
/// stays as it is. If that kladde file exists, asks whether to replace it
/// with the import, to open it instead, or to cancel.
///
/// Returns the document to show — `doc`, or the existing kladde file's — and
/// whether the user cancelled.
std::pair<std::unique_ptr<SPDocument>, bool> import(std::unique_ptr<SPDocument> doc, std::string const &source,
                                                     Gtk::Window *parent);

} // namespace Inkscape::Kladde

#endif // INKSCAPE_KLADDE_UI_H
