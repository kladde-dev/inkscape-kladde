// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A kladde-backed document records every change to its XML tree, so that the
 * kladde file reopens as the document was, whether or not it was closed.
 */
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>

#include "document.h"
#include "inkscape.h"
#include "kladde-io/backing.h"
#include "kladde-io/svg-values.h"
#include "xml/document.h"
#include "xml/node.h"
#include "xml/repr.h"

using namespace Inkscape;

namespace {

std::string const svg = INKSCAPE_TESTS_DIR "/data/doc_import/multi_content_4.svg";

std::string temp_path(char const *name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

std::string serialized(SPDocument &doc)
{
    return sp_repr_save_buf(doc.getReprDoc()).raw();
}

std::vector<char> bytes_of(std::string const &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void write_bytes(std::string const &path, std::vector<char> const &bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

/// Edits of every kind Inkscape makes to its tree.
void edit(SPDocument &doc)
{
    auto rdoc = doc.getReprDoc();
    auto root = doc.getReprRoot();
    // Attributes: set, change in the middle, remove.
    root->setAttribute("data-kladde", "a long value that changes in the middle");
    root->setAttribute("data-kladde", "a long value that CHANGES in the middle");
    root->setAttribute("data-gone", "x");
    root->setAttribute("data-gone", nullptr);
    // A subtree appended, then edited through its own nodes.
    auto g = rdoc->createElement("svg:g");
    g->setAttribute("id", "kladde-group");
    auto rect = rdoc->createElement("svg:rect");
    rect->setAttribute("width", "10");
    g->appendChild(rect);
    auto text = rdoc->createElement("svg:text");
    auto content = rdoc->createTextNode("hello");
    text->appendChild(content);
    g->appendChild(text);
    root->appendChild(g);
    rect->setAttribute("width", "20");
    content->setContent("hello, kladde");
    // Reordering, and removal.
    g->changeOrder(text, nullptr);
    root->changeOrder(g, nullptr);
    auto comment = rdoc->createComment(" a comment ");
    root->appendChild(comment);
    root->removeChild(comment);
    GC::release(content);
    GC::release(text);
    GC::release(rect);
    GC::release(g);
    GC::release(comment);
}

} // namespace

class KladdeBacking : public ::testing::Test
{
protected:
    void SetUp() override { Application::create(false); }
};

TEST_F(KladdeBacking, ReopensAsTheDocumentWasAfterClosing)
{
    auto path = temp_path("kladde-backing-closed.kladde");
    std::string expected;
    {
        auto doc = SPDocument::createNewDoc(svg.c_str());
        ASSERT_NE(doc, nullptr);
        ASSERT_NE(Kladde::Backing::create(*doc, path), nullptr);
        edit(*doc);
        expected = serialized(*doc);
    }
    auto reopened = Kladde::Backing::open(path);
    ASSERT_NE(reopened, nullptr);
    EXPECT_EQ(serialized(*reopened), expected);
    ASSERT_NE(reopened->getKladdeBacking(), nullptr);
}

TEST_F(KladdeBacking, ReopensAsTheDocumentWasAfterACrash)
{
    auto path = temp_path("kladde-backing-live.kladde");
    auto crashed = temp_path("kladde-backing-crashed.kladde");
    auto doc = SPDocument::createNewDoc(svg.c_str());
    ASSERT_NE(doc, nullptr);
    ASSERT_NE(Kladde::Backing::create(*doc, path), nullptr);
    edit(*doc);
    // What the file holds while the document is open, as a crash would leave
    // it: every change in the journal, nothing flushed since the creation.
    write_bytes(crashed, bytes_of(path));
    auto reopened = Kladde::Backing::open(crashed);
    ASSERT_NE(reopened, nullptr);
    EXPECT_EQ(serialized(*reopened), serialized(*doc));
}

TEST_F(KladdeBacking, EmbeddedDataIsKeptAsItsBytesAndReadsBackInOneLine)
{
    // Base64 of 3000 bytes, cut into lines of 76 characters as MIME does.
    std::vector<std::uint8_t> image(3000);
    for (std::size_t i = 0; i < image.size(); ++i) {
        image[i] = static_cast<std::uint8_t>(i * 7 + i / 256);
    }
    std::string base64;
    {
        Kladde::Attr one(std::in_place_index<std::size_t(Kladde::AttrKind::XlinkHref)>,
                         Kladde::Iri(Kladde::DataUri{"image/png", image}));
        base64 = Kladde::attribute_value(one).substr(std::string_view("data:image/png;base64,").size());
    }
    std::string broken;
    for (std::size_t at = 0; at < base64.size(); at += 76) {
        broken += base64.substr(at, 76) + '\n';
    }
    auto attr = Kladde::attribute_of("xlink:href", "data:image/png;base64," + broken);
    auto href = std::get_if<std::size_t(Kladde::AttrKind::XlinkHref)>(&attr);
    ASSERT_NE(href, nullptr);
    auto uri = std::get_if<Kladde::DataUri>(href);
    ASSERT_NE(uri, nullptr);
    EXPECT_EQ(uri->media, "image/png");
    EXPECT_EQ(uri->data, image);
    EXPECT_EQ(Kladde::attribute_value(attr), "data:image/png;base64," + base64);
    // A reference, and text that is no base64, stay text.
    for (auto value : {"#gradient", "data:image/png;base64,not base64!", "data:text/plain,hello"}) {
        auto other = Kladde::attribute_of("xlink:href", value);
        auto text = std::get_if<std::size_t(Kladde::AttrKind::XlinkHref)>(&other);
        ASSERT_NE(text, nullptr) << value;
        EXPECT_TRUE(std::holds_alternative<std::string>(*text)) << value;
        EXPECT_EQ(Kladde::attribute_value(other), value);
    }

    auto path = temp_path("kladde-backing-data.kladde");
    {
        auto doc = SPDocument::createNewDoc(svg.c_str());
        ASSERT_NE(doc, nullptr);
        ASSERT_NE(Kladde::Backing::create(*doc, path), nullptr);
        auto node = doc->getReprDoc()->createElement("svg:image");
        node->setAttribute("xlink:href", "data:image/png;base64," + broken);
        doc->getReprRoot()->appendChild(node);
        Inkscape::GC::release(node);
    }
    auto reopened = Kladde::Backing::open(path);
    ASSERT_NE(reopened, nullptr);
    auto root = reopened->getReprRoot();
    Inkscape::XML::Node const *found = nullptr;
    for (auto child = root->firstChild(); child; child = child->next()) {
        if (std::string_view(child->name()) == "svg:image") {
            found = child;
        }
    }
    ASSERT_NE(found, nullptr);
    char const *value = found->attribute("xlink:href");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(std::string(value), "data:image/png;base64," + base64);
}

TEST_F(KladdeBacking, EditsAfterReopeningAreRecordedToo)
{
    auto path = temp_path("kladde-backing-again.kladde");
    {
        auto doc = SPDocument::createNewDoc(svg.c_str());
        ASSERT_NE(doc, nullptr);
        ASSERT_NE(Kladde::Backing::create(*doc, path), nullptr);
    }
    std::string expected;
    {
        auto doc = Kladde::Backing::open(path);
        ASSERT_NE(doc, nullptr);
        edit(*doc);
        expected = serialized(*doc);
    }
    auto reopened = Kladde::Backing::open(path);
    ASSERT_NE(reopened, nullptr);
    EXPECT_EQ(serialized(*reopened), expected);
}

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
