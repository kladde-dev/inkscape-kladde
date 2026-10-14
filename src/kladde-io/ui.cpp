// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * The user-facing side of kladde-backed documents; see ui.h.
 */
#include "kladde-io/ui.h"

#include <giomm/file.h>
#include <giomm/liststore.h>
#include <glibmm/fileutils.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/messagedialog.h>

#include "document.h"
#include "kladde-io/backing.h"
#include "ui/dialog-run.h"
#include "ui/dialog/choose-file.h"

namespace Inkscape::Kladde {

std::string ask_new_path(Gtk::Window *parent)
{
    auto filter = Gtk::FileFilter::create();
    filter->set_name(_("kladde documents"));
    filter->add_pattern("*.kladde");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    static std::string folder;
    if (folder.empty()) {
        folder = Glib::get_user_special_dir(Glib::UserDirectory::DOCUMENTS);
    }
    auto file = choose_file_save(_("Create the new document's kladde file"), parent, filters, "drawing.kladde", folder);
    if (!file) {
        return {};
    }
    auto path = file->get_path();
    if (!is_kladde_path(path)) {
        path += extension;
    }
    return path;
}

std::string import_path(std::string const &source)
{
    auto dir = Glib::path_get_dirname(source);
    auto name = Glib::path_get_basename(source);
    auto dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) {
        name.resize(dot);
    }
    return Glib::build_filename(dir, name + extension);
}

namespace {

enum class Existing { replace, open, cancel };

Existing ask_existing(Gtk::Window *parent, std::string const &path, std::string const &source)
{
    auto message = Glib::ustring::compose(
        _("Opening %1 makes a kladde file of it, which keeps every change as you make it.\n\n%2 exists already."),
        Glib::path_get_basename(source), Glib::path_get_basename(path));
    auto dialog = parent ? Gtk::MessageDialog(*parent, message, false, Gtk::MessageType::QUESTION,
                                              Gtk::ButtonsType::NONE, true)
                         : Gtk::MessageDialog(message, false, Gtk::MessageType::QUESTION, Gtk::ButtonsType::NONE, true);
    dialog.set_title(_("kladde file exists"));
    dialog.add_button(_("_Cancel"), Gtk::ResponseType::CANCEL);
    dialog.add_button(_("_Open the existing kladde file"), Gtk::ResponseType::NO);
    dialog.add_button(_("_Replace it with the import"), Gtk::ResponseType::YES);
    dialog.set_default_response(Gtk::ResponseType::NO);
    switch (Inkscape::UI::dialog_run(dialog)) {
        case Gtk::ResponseType::YES: return Existing::replace;
        case Gtk::ResponseType::NO: return Existing::open;
        default: return Existing::cancel;
    }
}

} // namespace

std::pair<std::unique_ptr<SPDocument>, bool> import(std::unique_ptr<SPDocument> doc, std::string const &source,
                                                     Gtk::Window *parent)
{
    auto path = import_path(source);
    if (Glib::file_test(path, Glib::FileTest::EXISTS)) {
        switch (ask_existing(parent, path, source)) {
            case Existing::cancel:
                return {nullptr, true};
            case Existing::open:
                return {Backing::open(path), false};
            case Existing::replace:
                break;
        }
    }
    if (Backing::create(*doc, path)) {
        // Saving, from now on, means nothing more than flushing the kladde
        // file; saving as SVG remains Save As.
        doc->setDocumentFilename(path.c_str());
        doc->setModifiedSinceSave(false);
    }
    return {std::move(doc), false};
}

} // namespace Inkscape::Kladde
