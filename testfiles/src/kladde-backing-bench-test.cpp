// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Measures kladde-backed documents on real drawings: importing, editing as a
 * drawing program edits (drags, style changes, reorders, duplicates and
 * deletions, through Inkscape's XML tree), closing and reopening.
 *
 * Runs only with KLADDE_SVG_CORPUS set to a directory of .svg files, such as
 * kladde-rs's crates/kladde-svg/corpus/drawings, and prints one line per
 * drawing, followed by how much of the drawing's attributes the schema holds
 * as typed values; it asserts only that the file reopens as the document was.
 * KLADDE_BENCH_STEPS sets the number of edits, KLADDE_JOURNAL_BUDGET_PAGES
 * fixes the journal budget, and KLADDE_BENCH_SAVE_DIR names a directory to
 * keep each drawing in, as SVG, as the edits leave it.
 */
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <malloc.h>
#include <algorithm>
#include <map>
#include <random>

#include "document.h"
#include "inkscape.h"
#include "kladde-io/backing.h"
#include "kladde-io/svg-values.h"
#include "xml/document.h"
#include "xml/node.h"
#include "xml/repr.h"

using namespace Inkscape;
namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

std::size_t heap_bytes()
{
    return mallinfo2().uordblks;
}

/// How much of a document's attributes the typed schema types.
struct TypedShare
{
    std::size_t attributes = 0;
    std::size_t typed = 0;
    /// Typed, with a value other than text (`id`, `class`, `href` and
    /// `font-family` are typed as text).
    std::size_t typed_values = 0;
    std::size_t text_bytes = 0;
    std::size_t typed_text_bytes = 0;
    std::size_t declarations = 0;
    std::size_t typed_declarations = 0;
    /// Attributes the schema knows by name that are kept verbatim, since
    /// their value does not format back as written.
    std::map<std::string, std::size_t> untyped_known;
};

bool is_text(Kladde::Attr const &a)
{
    using K = Kladde::AttrKind;
    auto k = static_cast<K>(a.index());
    return k == K::Id || k == K::Class || k == K::Href || k == K::XlinkHref || k == K::FontFamily;
}

void count(Kladde::Element const &e, TypedShare &share)
{
    for (auto const &a : e.attributes) {
        auto bytes = Kladde::attribute_name(a).size() + Kladde::attribute_value(a).size();
        ++share.attributes;
        share.text_bytes += bytes;
        if (Kladde::is_typed(a)) {
            ++share.typed;
            share.typed_text_bytes += bytes;
            share.typed_values += is_text(a) ? 0 : 1;
        } else if (auto const &other = std::get<Kladde::Other>(a); Kladde::types_attribute(other.name)) {
            ++share.untyped_known[other.name];
        }
        if (auto style = std::get_if<std::size_t(Kladde::AttrKind::Style)>(&a)) {
            for (auto const &d : *style) {
                ++share.declarations;
                share.typed_declarations += std::holds_alternative<Kladde::Other>(d) ? 0 : 1;
            }
        }
    }
    for (auto const &child : e.children) {
        if (auto element = std::get_if<kladde::Box<Kladde::Element>>(&child)) {
            count(**element, share);
        }
    }
}

void elements_of(XML::Node *node, std::vector<XML::Node *> &out)
{
    for (auto c = node->firstChild(); c; c = c->next()) {
        if (c->type() == XML::NodeType::ELEMENT_NODE) {
            out.push_back(c);
            elements_of(c, out);
        }
    }
}

/// A drawing program's edits: mostly drags of one element at a time, each a
/// run of small transform changes, then style changes, reorders,
/// duplicates and deletions.
/// Where `edit` puts how long each move of a drag took, in µs, if anywhere.
std::vector<double> *drag_latencies = nullptr;

void edit(SPDocument &doc, int steps, std::mt19937 &rng)
{
    auto root = doc.getReprRoot();
    std::vector<XML::Node *> elements;
    elements_of(root, elements);
    auto pick = [&] { return elements[std::uniform_int_distribution<std::size_t>(0, elements.size() - 1)(rng)]; };
    int done = 0;
    while (done < steps && !elements.empty()) {
        auto kind = std::uniform_int_distribution<int>(0, 99)(rng);
        auto e = pick();
        if (kind < 80) {
            // A drag: twenty moves of one element.
            for (int i = 0; i < 20 && done < steps; ++i, ++done) {
                auto t = "translate(" + std::to_string(i * 1.25) + "," + std::to_string(i * -0.75) + ")";
                auto t0 = Clock::now();
                e->setAttribute("transform", t);
                if (drag_latencies) {
                    drag_latencies->push_back(ms_since(t0) * 1000.0);
                }
            }
        } else if (kind < 90) {
            e->setAttribute("style", "fill:#" + std::to_string(100000 + done % 899999) + ";stroke:none");
            ++done;
        } else if (kind < 94) {
            if (auto parent = e->parent(); parent && e->prev()) {
                parent->changeOrder(e, nullptr);
            }
            ++done;
        } else if (kind < 97) {
            if (auto parent = e->parent(); parent && parent != doc.getReprDoc()) {
                auto copy = e->duplicate(doc.getReprDoc());
                parent->appendChild(copy);
                GC::release(copy);
            }
            ++done;
        } else {
            if (auto parent = e->parent(); parent && e != root && e->firstChild() == nullptr) {
                parent->removeChild(e);
                elements.clear();
                elements_of(root, elements);
            }
            ++done;
        }
    }
}

} // namespace

TEST(KladdeBackingBench, Drawings)
{
    auto corpus = std::getenv("KLADDE_SVG_CORPUS");
    if (!corpus) {
        GTEST_SKIP() << "KLADDE_SVG_CORPUS is not set";
    }
    Application::create(false);
    int steps = std::getenv("KLADDE_BENCH_STEPS") ? std::atoi(std::getenv("KLADDE_BENCH_STEPS")) : 10000;
    kladde::Options options;
    if (auto budget = std::getenv("KLADDE_JOURNAL_BUDGET_PAGES")) {
        options.journal_budget_pages = static_cast<std::uint32_t>(std::atoi(budget));
    }
    // Where to keep each drawing as the edits leave it, as SVG, if anywhere.
    auto save_dir = std::getenv("KLADDE_BENCH_SAVE_DIR");
    if (options.journal_budget_pages) {
        std::printf("journal budget: %u pages\n", *options.journal_budget_pages);
    } else {
        std::printf("journal budget: sized to the drawing (budget_pages, after the edits)\n");
    }
    std::printf("drawing svg_KB parse_ms doc_heap_KB import_ms import_heap_KB value_KB edits plain_edit_us edit_us "
                "journal_B_per_edit flushes file_KB closed_KB svg_save_ms reopen_ms\n");
    for (auto const &entry : fs::directory_iterator(corpus)) {
        if (entry.path().extension() != ".svg") {
            continue;
        }
        auto svg = entry.path().string();
        auto path = (fs::temp_directory_path() / (entry.path().stem().string() + "-bench.kladde")).string();
        // The same edits without a backing: what Inkscape spends on them.
        double plain_ms = 0;
        {
            auto plain = SPDocument::createNewDoc(svg.c_str());
            ASSERT_NE(plain, nullptr);
            std::mt19937 rng(7);
            auto t0 = Clock::now();
            edit(*plain, steps, rng);
            plain_ms = ms_since(t0);
        }
        auto heap_doc = heap_bytes();
        auto t = Clock::now();
        auto doc = SPDocument::createNewDoc(svg.c_str());
        ASSERT_NE(doc, nullptr);
        double parse_ms = ms_since(t);
        auto doc_heap = heap_bytes() - heap_doc;
        auto heap0 = heap_bytes();
        t = Clock::now();
        auto backing = Kladde::Backing::create(*doc, path, options);
        ASSERT_NE(backing, nullptr);
        double import_ms = ms_since(t);
        auto import_heap = heap_bytes() - heap0;
        auto value_size = backing->encoded_size();
        TypedShare share;
        count(backing->root(), share);
        auto before = backing->stats();
        auto live_kb = backing->encoded_size() / 1024.0;
        std::mt19937 rng(7);
        std::vector<double> latencies;
        drag_latencies = &latencies;
        t = Clock::now();
        edit(*doc, steps, rng);
        double edit_ms = ms_since(t);
        drag_latencies = nullptr;
        std::sort(latencies.begin(), latencies.end());
        auto quantile = [&](double q) { return latencies.empty() ? 0.0 : latencies[std::size_t(q * (latencies.size() - 1))]; };
        auto after = backing->stats();
        TypedShare edited;
        count(backing->root(), edited);
        auto live_file = fs::file_size(path);
        t = Clock::now();
        auto saved = sp_repr_save_buf(doc->getReprDoc());
        double save_ms = ms_since(t);
        std::string expected = saved.raw();
        if (save_dir) {
            std::ofstream(fs::path(save_dir) / entry.path().filename(), std::ios::binary) << expected;
        }
        auto budget_pages = options.journal_budget(backing->stats().sequence_bytes);
        auto edited_size = backing->encoded_size();
        t = Clock::now();
        doc.reset();  // closes the file
        double close_ms = ms_since(t);
        auto closed = fs::file_size(path);
        t = Clock::now();
        auto reopened = Kladde::Backing::open(path, options);
        double reopen_ms = ms_since(t);
        ASSERT_NE(reopened, nullptr);
        EXPECT_EQ(sp_repr_save_buf(reopened->getReprDoc()).raw(), expected);
        std::printf("%s %.0f %.1f %.0f %.1f %.0f %.1f %d %.2f %.2f %.1f %llu %.0f %.0f %.1f %.1f "
                    "live_KB=%.0f edited_KB=%.0f close_ms=%.1f budget_pages=%u drag_p50_us=%.1f p99_us=%.1f "
                    "p999_us=%.1f max_us=%.0f\n",
                    entry.path().stem().c_str(), fs::file_size(entry.path()) / 1024.0, parse_ms,
                    doc_heap / 1024.0, import_ms, import_heap / 1024.0, value_size / 1024.0, steps,
                    plain_ms * 1000.0 / steps,
                    edit_ms * 1000.0 / steps, double(after.journal_bytes - before.journal_bytes) / steps,
                    (unsigned long long)(after.flushes - before.flushes), live_file / 1024.0, closed / 1024.0,
                    save_ms, reopen_ms, live_kb, edited_size / 1024.0, close_ms, budget_pages, quantile(0.5), quantile(0.99), quantile(0.999),
                    latencies.empty() ? 0.0 : latencies.back());
        auto percent = [](std::size_t part, std::size_t whole) { return whole ? 100.0 * part / whole : 0.0; };
        std::printf("  typed: %zu attributes, %.1f%% typed (%.1f%% with a value other than text), %.1f%% of their "
                    "text; %zu style declarations, %.1f%% typed; verbatim though known:",
                    share.attributes, percent(share.typed, share.attributes),
                    percent(share.typed_values, share.attributes),
                    percent(share.typed_text_bytes, share.text_bytes), share.declarations,
                    percent(share.typed_declarations, share.declarations));
        for (auto const &[name, n] : share.untyped_known) {
            std::printf(" %s=%zu", name.c_str(), n);
        }
        std::printf("; after the edits, %zu attributes, %.1f%% typed\n", edited.attributes,
                    percent(edited.typed, edited.attributes));
        reopened.reset();
        fs::remove(path);
    }
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
