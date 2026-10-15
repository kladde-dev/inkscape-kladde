// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A kladde file that backs a document; see backing.h.
 */
#include "kladde-io/backing.h"

#include <cstring>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <iostream>

#include "document.h"
#include "kladde-io/svg-values.h"
#include "xml/node.h"
#include "xml/simple-document.h"
#include "xml/text-node.h"

namespace Inkscape::Kladde {

namespace {

/// The share of the journal's budget past which the idle callback flushes.
/// Well above zero, so that pauses between gestures do not end every
/// journal, and with it the cuts that moving nodes makes.
constexpr double idle_flush_fraction = 0.5;

std::string str(char const *s) { return s ? s : ""; }

/// Replaces the value `m` refers to with `value`, recording only the range
/// of its encoding between the common prefix and suffix of the old and the
/// new encoding, as a text's `set` does for its bytes; `Mut::set` records the
/// whole encoding, which for a path of a thousand segments whose one number
/// changed is a thousand segments.
template <class M, class T>
void assign(M &&m, T value)
{
    auto old_bytes = kladde::to_bytes(m.get());
    auto new_bytes = kladde::to_bytes(value);
    if (old_bytes == new_bytes) {
        return;
    }
    auto &region = m.region();
    if (!kladde::check(region.backend(), kladde::splice_changed(region, 0, old_bytes, new_bytes))) {
        return;
    }
    // The handle hands out its value only for reading; the value is ours,
    // and the store now holds its new encoding.
    const_cast<T &>(m.get()) = std::move(value);
}

// ---------------------------------------------------------------- Inkscape -> kladde

Element element_of(XML::Node const &node);

Node node_of(XML::Node const &node)
{
    switch (node.type()) {
        case XML::NodeType::ELEMENT_NODE:
            return kladde::Box<Element>(element_of(node));
        case XML::NodeType::TEXT_NODE: {
            auto text = dynamic_cast<XML::TextNode const *>(&node);
            return Text{str(node.content()), text && text->is_CData()};
        }
        case XML::NodeType::COMMENT_NODE:
            return Comment{str(node.content())};
        case XML::NodeType::PI_NODE:
            return ProcessingInstruction{str(node.name()), str(node.content())};
        case XML::NodeType::DOCUMENT_NODE:
            break;
    }
    return Comment{};
}

/// The element's name, attributes and children; the document node is an
/// element without a name.
Element element_of(XML::Node const &node)
{
    Element e;
    e.name = node.type() == XML::NodeType::DOCUMENT_NODE ? Name(std::string()) : name_of(str(node.name()));
    std::vector<Attr> attributes;
    for (auto const &a : node.attributeList()) {
        attributes.push_back(attribute_of(str(g_quark_to_string(a.key)), str(a.value)));
    }
    e.attributes = kladde::Vector<Attr>(std::move(attributes));
    std::vector<Node> children;
    for (auto child = node.firstChild(); child; child = child->next()) {
        children.push_back(node_of(*child));
    }
    e.children = kladde::List<Node>(std::move(children));
    return e;
}

// ---------------------------------------------------------------- kladde -> Inkscape

void build_children(XML::Document &doc, XML::Node &parent, Element const &e);

void set_attributes(XML::Node &node, Element const &e)
{
    for (auto const &a : e.attributes) {
        node.setAttribute(std::string(attribute_name(a)), attribute_value(a));
    }
}

XML::Node *build_node(XML::Document &doc, Node const &n)
{
    return std::visit(
        [&](auto const &v) -> XML::Node * {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, kladde::Box<Element>>) {
                auto node = doc.createElement(qualified_name(v->name).c_str());
                set_attributes(*node, *v);
                build_children(doc, *node, *v);
                return node;
            } else if constexpr (std::is_same_v<V, Text>) {
                return doc.createTextNode(v.content.c_str(), v.cdata);
            } else if constexpr (std::is_same_v<V, Comment>) {
                return doc.createComment(v.content.c_str());
            } else {
                return doc.createPI(v.target.c_str(), v.content.c_str());
            }
        },
        n);
}

void build_children(XML::Document &doc, XML::Node &parent, Element const &e)
{
    for (auto const &child : e.children) {
        auto node = build_node(doc, child);
        parent.appendChild(node);
        GC::release(node);
    }
}

Element const *as_element(Node const &n)
{
    auto box = std::get_if<kladde::Box<Element>>(&n);
    return box ? box->get() : nullptr;
}

} // namespace

bool is_kladde_path(std::string const &path)
{
    auto n = std::char_traits<char>::length(extension);
    return path.size() > n && path.compare(path.size() - n, n, extension) == 0;
}

// ---------------------------------------------------------------- the backing

struct Backing::Impl
{
    explicit Impl(kladde::File<Element> f)
        : file(std::move(f))
    {}

    kladde::File<Element> file;
    std::string path;
    SPDocument *doc = nullptr;
    XML::Document *rdoc = nullptr;
    /// Each node of Inkscape's tree but the document node, to the element of
    /// the kladde list that holds its counterpart.
    std::unordered_map<XML::Node const *, kladde::ListElement<Node> *> refs;
    sigc::connection idle;
    /// Set once writing failed: the document is no longer backed.
    bool failed = false;

    /// Maps `parent`'s children, and theirs, to the elements of `list`.
    void register_tree(XML::Node const &parent, kladde::List<Node> const &list)
    {
        std::size_t i = 0;
        for (auto child = parent.firstChild(); child; child = child->next(), ++i) {
            auto e = list.element_at(i);
            refs[child] = e;
            if (auto el = as_element(e->value)) {
                register_tree(*child, el->children);
            }
        }
    }

    void unregister_tree(XML::Node const &node)
    {
        refs.erase(&node);
        for (auto child = node.firstChild(); child; child = child->next()) {
            unregister_tree(*child);
        }
    }

    kladde::ListElement<Node> *find(XML::Node const *node)
    {
        auto it = refs.find(node);
        return it == refs.end() ? nullptr : it->second;
    }

    /// Calls `f` with the handle of the element that is `node`'s
    /// counterpart; false if there is none.
    template <class F>
    bool with_element(XML::Node &node, F &&f)
    {
        if (&node == rdoc) {
            auto root = file.edit();
            f(static_cast<kladde::Mut<Element> &>(root));
            return true;
        }
        auto e = find(&node);
        if (!e) {
            return false;
        }
        return kladde::Ref<Node>(e).edit().template if_holds<kladde::Box<Element>>(
            [&](kladde::Mut<kladde::Box<Element>> box) { f(static_cast<kladde::Mut<Element> &>(box)); });
    }

    /// Rewrites the whole file from Inkscape's tree: the way back to a known
    /// state when an event does not fit what the file holds.
    void resync()
    {
        auto fresh = element_of(*rdoc);
        if (!(fresh == file.get())) {
            file.edit().set(std::move(fresh));
        }
        refs.clear();
        register_tree(*rdoc, file.get().children);
    }
};

Backing::Backing(std::string path, std::unique_ptr<Impl> impl)
    : _path(std::move(path))
    , _impl(std::move(impl))
{}

Backing::~Backing()
{
    _impl->idle.disconnect();
    if (_impl->rdoc) {
        _impl->rdoc->removeSubtreeObserver(*this);
    }
    if (!_impl->failed) {
        try {
            // An ordinary close, which a crash never gets to: the file is
            // replaced by the smallest file that holds the drawing.
            _impl->file.close_consolidated();
        } catch (kladde::exception const &e) {
            std::cerr << "kladde: closing " << _path << " failed: " << e.what() << std::endl;
        }
    }
}

Backing *Backing::create(SPDocument &doc, std::string const &path, kladde::Options const &options)
{
    auto file = kladde::File<Element>::create(path, element_of(*doc.getReprDoc()), options);
    if (!file) {
        std::cerr << "kladde: cannot create " << path << ": " << file.error().what() << std::endl;
        return nullptr;
    }
    auto backing = std::unique_ptr<Backing>(new Backing(path, std::make_unique<Impl>(std::move(*file))));
    auto raw = backing.get();
    backing->attach(doc);
    doc.setKladdeBacking(std::move(backing));
    return raw;
}

std::unique_ptr<SPDocument> Backing::open(std::string const &path, kladde::Options const &options)
{
    auto file = kladde::File<Element>::open(path, options);
    if (!file) {
        std::cerr << "kladde: cannot open " << path << ": " << file.error().what() << std::endl;
        return nullptr;
    }
    auto const &root = file->get();
    auto rdoc = new XML::SimpleDocument();
    set_attributes(*rdoc, root);
    build_children(*rdoc, *rdoc, root);
    if (!rdoc->root() || std::strcmp(rdoc->root()->name(), "svg:svg") != 0) {
        std::cerr << "kladde: " << path << " holds no SVG document" << std::endl;
        GC::release(rdoc);
        return nullptr;
    }
    auto base = Glib::path_get_dirname(path);
    auto name = Glib::path_get_basename(path);
    auto doc = SPDocument::createDoc(rdoc, path.c_str(), base.c_str(), name.c_str());
    if (!doc) {
        return nullptr;
    }
    auto backing = std::unique_ptr<Backing>(new Backing(path, std::make_unique<Impl>(std::move(*file))));
    backing->attach(*doc);
    doc->setKladdeBacking(std::move(backing));
    return doc;
}

void Backing::attach(SPDocument &doc)
{
    _impl->path = _path;
    _impl->doc = &doc;
    _impl->rdoc = doc.getReprDoc();
    // Building the document may have changed its tree already (a named view,
    // the version attributes), before anything observed it.
    _impl->resync();
    _impl->rdoc->addSubtreeObserver(*this);
}

void Backing::flush()
{
    if (_impl->failed) {
        return;
    }
    try {
        _impl->file.flush();
    } catch (kladde::exception const &e) {
        _impl->failed = true;
        std::cerr << "kladde: flushing " << _path << " failed: " << e.what() << std::endl;
    }
}

kladde::Stats Backing::stats() const
{
    return _impl->file.stats();
}

std::size_t Backing::encoded_size() const
{
    return kladde::encoded_len(_impl->file.get());
}

Element const &Backing::root() const
{
    return _impl->file.get();
}

namespace {

/// Runs `f`, which records a change; a failure of the disk ends the backing,
/// and a change that does not fit the file rewrites it whole.
template <class Impl, class F>
void record(Impl &impl, std::string const &path, F &&f)
{
    if (impl.failed) {
        return;
    }
    try {
        if (!f()) {
            std::cerr << "kladde: an event for a node the file does not know; rewriting " << path << std::endl;
            impl.resync();
        }
        if (!impl.idle.connected()) {
            impl.idle = Glib::signal_idle().connect(
                [&impl] {
                    try {
                        if (!impl.failed) {
                            impl.file.flush_if_journal_exceeds(idle_flush_fraction);
                            // Everything is recorded: nothing is left to
                            // save to the kladde file.
                            auto name = impl.doc->getDocumentFilename();
                            if (name && impl.path == name && impl.doc->isModifiedSinceSave()) {
                                impl.doc->setModifiedSinceSave(false);
                            }
                        }
                    } catch (kladde::exception const &e) {
                        impl.failed = true;
                        std::cerr << "kladde: flushing failed: " << e.what() << std::endl;
                    }
                    return false;
                },
                Glib::PRIORITY_LOW);
        }
    } catch (kladde::exception const &e) {
        impl.failed = true;
        std::cerr << "kladde: recording a change failed, and changes are no longer saved: " << e.what()
                  << std::endl;
    }
}

} // namespace

void Backing::notifyChildAdded(XML::Node &node, XML::Node &child, XML::Node *prev)
{
    record(*_impl, _path, [&] {
        std::size_t index = 0;
        if (prev) {
            auto p = _impl->find(prev);
            if (!p) {
                return false;
            }
            index = p->index() + 1;
        }
        kladde::ListElement<Node> *added = nullptr;
        bool known = _impl->with_element(node, [&](kladde::Mut<Element> &e) {
            added = e.children().insert(index, node_of(child));
        });
        if (!known || !added) {
            return false;
        }
        _impl->refs[&child] = added;
        if (auto el = as_element(added->value)) {
            _impl->register_tree(child, el->children);
        }
        return true;
    });
}

void Backing::notifyChildRemoved(XML::Node &node, XML::Node &child, XML::Node * /*prev*/)
{
    record(*_impl, _path, [&] {
        auto e = _impl->find(&child);
        if (!e) {
            return false;
        }
        auto index = e->index();
        _impl->unregister_tree(child);
        return _impl->with_element(node, [&](kladde::Mut<Element> &el) { el.children().erase(index); });
    });
}

void Backing::notifyChildOrderChanged(XML::Node &node, XML::Node &child, XML::Node * /*old_prev*/,
                                      XML::Node *new_prev)
{
    record(*_impl, _path, [&] {
        auto e = _impl->find(&child);
        if (!e) {
            return false;
        }
        auto from = e->index();
        std::size_t to = 0;
        if (new_prev) {
            auto p = _impl->find(new_prev);
            if (!p) {
                return false;
            }
            to = p->index() < from ? p->index() + 1 : p->index();
        }
        return _impl->with_element(node, [&](kladde::Mut<Element> &el) { el.children().move(from, to); });
    });
}

void Backing::notifyContentChanged(XML::Node &node, Util::ptr_shared /*old_content*/, Util::ptr_shared new_content)
{
    record(*_impl, _path, [&] {
        auto e = _impl->find(&node);
        if (!e) {
            return false;
        }
        auto content = str(new_content);
        kladde::Ref<Node>(e).edit().visit([&](auto alt) {
            using A = std::decay_t<decltype(alt.get())>;
            if constexpr (!std::is_same_v<A, Element>) {
                alt.content().set(content);
            }
        });
        return true;
    });
}

void Backing::notifyAttributeChanged(XML::Node &node, GQuark name, Util::ptr_shared /*old_value*/,
                                     Util::ptr_shared new_value)
{
    record(*_impl, _path, [&] {
        std::string_view key = g_quark_to_string(name);
        return _impl->with_element(node, [&](kladde::Mut<Element> &el) {
            auto const &attributes = el->attributes;
            std::size_t i = 0;
            while (i < attributes.size() && attribute_name(attributes[i]) != key) {
                ++i;
            }
            auto list = el.attributes();
            if (!new_value) {
                if (i < attributes.size()) {
                    list.erase(i);
                }
            } else if (i < attributes.size()) {
                assign(list.at(i), attribute_of(key, new_value.pointer()));
            } else {
                list.push_back(attribute_of(key, new_value.pointer()));
            }
        });
    });
}

void Backing::notifyElementNameChanged(XML::Node &node, GQuark /*old_name*/, GQuark new_name)
{
    record(*_impl, _path, [&] {
        return _impl->with_element(node, [&](kladde::Mut<Element> &el) {
            assign(el.name(), name_of(str(g_quark_to_string(new_name))));
        });
    });
}

} // namespace Inkscape::Kladde
