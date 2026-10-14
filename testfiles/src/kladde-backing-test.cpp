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
