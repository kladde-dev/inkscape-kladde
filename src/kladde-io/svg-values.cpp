// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Between Inkscape's attribute strings and the typed SVG schema; see
 * svg-values.h.
 *
 * Every value is parsed leniently and formatted in one way per type (or, for
 * path data, in the way the path's `layout` records); the caller keeps the
 * typed value only if the formatted text is the original. A value written
 * some other way — with exponents, leading `+` or `.`, a named color, extra
 * spaces — therefore ends up as `Other`, never altered.
 */
#include "kladde-io/svg-values.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Inkscape::Kladde {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// ---------------------------------------------------------------- numbers

/// Reads `-?digits(.digits)?` at `pos`, advancing it.
bool read_number(std::string_view s, std::size_t &pos, Number &n)
{
    auto p = pos;
    bool negative = p < s.size() && s[p] == '-';
    if (negative) {
        ++p;
    }
    std::int64_t digits = 0;
    unsigned significant = 0;
    unsigned scale = 0;
    auto take = [&](bool fraction) {
        auto from = p;
        for (; p < s.size() && is_digit(s[p]); ++p) {
            if (digits != 0 || s[p] != '0') {
                ++significant;
            }
            if (significant > 18) {
                return false;
            }
            digits = digits * 10 + (s[p] - '0');
            if (fraction) {
                ++scale;
            }
        }
        return p > from;
    };
    if (!take(false)) {
        return false;
    }
    if (p < s.size() && s[p] == '.') {
        ++p;
        if (!take(true)) {
            return false;
        }
    }
    if (scale > std::numeric_limits<std::uint8_t>::max() || digits > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    n.digits = static_cast<std::int32_t>(negative ? -digits : digits);
    n.scale = static_cast<std::uint8_t>(scale);
    pos = p;
    return true;
}

void format(Number const &n, std::string &out)
{
    std::int64_t v = n.digits;
    if (v < 0) {
        out += '-';
        v = -v;
    }
    auto d = std::to_string(v);
    if (n.scale == 0) {
        out += d;
        return;
    }
    if (d.size() <= n.scale) {
        d.insert(0, n.scale + 1 - d.size(), '0');
    }
    out.append(d, 0, d.size() - n.scale);
    out += '.';
    out.append(d, d.size() - n.scale, std::string::npos);
}

bool parse(std::string_view s, Number &n)
{
    std::size_t pos = 0;
    return read_number(s, pos, n) && pos == s.size();
}

/// Skips spaces and commas, the separators of number lists.
void skip_separators(std::string_view s, std::size_t &pos)
{
    while (pos < s.size() && (s[pos] == ',' || std::isspace(static_cast<unsigned char>(s[pos])))) {
        ++pos;
    }
}

/// Reads numbers separated by spaces and commas, up to the end of `s`.
bool read_numbers(std::string_view s, std::vector<Number> &out)
{
    std::size_t pos = 0;
    skip_separators(s, pos);
    while (pos < s.size()) {
        Number n;
        if (!read_number(s, pos, n)) {
            return false;
        }
        out.push_back(n);
        skip_separators(s, pos);
    }
    return true;
}

// ---------------------------------------------------------------- lengths

constexpr std::array<std::pair<LengthUnit, std::string_view>, 10> units = {{
    {LengthUnit::none, ""},
    {LengthUnit::em, "em"},
    {LengthUnit::ex, "ex"},
    {LengthUnit::px, "px"},
    {LengthUnit::in, "in"},
    {LengthUnit::cm, "cm"},
    {LengthUnit::mm, "mm"},
    {LengthUnit::pt, "pt"},
    {LengthUnit::pc, "pc"},
    {LengthUnit::percent, "%"},
}};

bool read_length(std::string_view s, std::size_t &pos, Length &l)
{
    if (!read_number(s, pos, l.value)) {
        return false;
    }
    auto rest = s.substr(pos);
    for (auto const &[unit, suffix] : units) {
        if (!suffix.empty() && rest.starts_with(suffix)) {
            l.unit = unit;
            pos += suffix.size();
            return true;
        }
    }
    l.unit = LengthUnit::none;
    return true;
}

bool parse(std::string_view s, Length &l)
{
    std::size_t pos = 0;
    return read_length(s, pos, l) && pos == s.size();
}

void format(Length const &l, std::string &out)
{
    format(l.value, out);
    for (auto const &[unit, suffix] : units) {
        if (unit == l.unit) {
            out += suffix;
        }
    }
}

bool parse(std::string_view s, Lengths &ls)
{
    std::vector<Length> items;
    std::size_t pos = 0;
    skip_separators(s, pos);
    while (pos < s.size()) {
        Length l;
        if (!read_length(s, pos, l)) {
            return false;
        }
        items.push_back(l);
        skip_separators(s, pos);
    }
    if (items.empty()) {
        return false;
    }
    ls = Lengths(std::move(items));
    return true;
}

void format(Lengths const &ls, std::string &out)
{
    bool first = true;
    for (auto const &l : ls) {
        if (!first) {
            out += ',';
        }
        first = false;
        format(l, out);
    }
}

bool parse(std::string_view s, PointList &points)
{
    std::vector<Number> numbers;
    if (!read_numbers(s, numbers) || numbers.size() % 2 != 0) {
        return false;
    }
    std::vector<Point> items;
    for (std::size_t i = 0; i < numbers.size(); i += 2) {
        items.push_back({numbers[i], numbers[i + 1]});
    }
    points = PointList(std::move(items));
    return true;
}

void format(PointList const &points, std::string &out)
{
    bool first = true;
    for (auto const &p : points) {
        if (!first) {
            out += ' ';
        }
        first = false;
        format(p.x, out);
        out += ',';
        format(p.y, out);
    }
}

bool parse(std::string_view s, ViewBox &v)
{
    std::vector<Number> numbers;
    if (!read_numbers(s, numbers) || numbers.size() != 4) {
        return false;
    }
    v = {numbers[0], numbers[1], numbers[2], numbers[3]};
    return true;
}

void format(ViewBox const &v, std::string &out)
{
    format(v.x, out);
    out += ' ';
    format(v.y, out);
    out += ' ';
    format(v.width, out);
    out += ' ';
    format(v.height, out);
}

// ---------------------------------------------------------------- keywords and colors

template <class E, std::size_t N>
bool parse_keyword(std::string_view s, E &e, std::array<std::pair<E, std::string_view>, N> const &names)
{
    for (auto const &[value, name] : names) {
        if (name == s) {
            e = value;
            return true;
        }
    }
    return false;
}

template <class E, std::size_t N>
void format_keyword(E e, std::string &out, std::array<std::pair<E, std::string_view>, N> const &names)
{
    for (auto const &[value, name] : names) {
        if (value == e) {
            out += name;
        }
    }
}

constexpr std::array<std::pair<LineCap, std::string_view>, 3> line_caps = {{
    {LineCap::butt, "butt"},
    {LineCap::round, "round"},
    {LineCap::square, "square"},
}};
constexpr std::array<std::pair<LineJoin, std::string_view>, 5> line_joins = {{
    {LineJoin::miter, "miter"},
    {LineJoin::miter_clip, "miter-clip"},
    {LineJoin::round, "round"},
    {LineJoin::bevel, "bevel"},
    {LineJoin::arcs, "arcs"},
}};
constexpr std::array<std::pair<FillRule, std::string_view>, 2> fill_rules = {{
    {FillRule::nonzero, "nonzero"},
    {FillRule::evenodd, "evenodd"},
}};
constexpr std::array<std::pair<PaintKeyword, std::string_view>, 3> paint_keywords = {{
    {PaintKeyword::none, "none"},
    {PaintKeyword::currentColor, "currentColor"},
    {PaintKeyword::inherit, "inherit"},
}};

bool parse(std::string_view s, LineCap &v) { return parse_keyword(s, v, line_caps); }
void format(LineCap v, std::string &out) { format_keyword(v, out, line_caps); }
bool parse(std::string_view s, LineJoin &v) { return parse_keyword(s, v, line_joins); }
void format(LineJoin v, std::string &out) { format_keyword(v, out, line_joins); }
bool parse(std::string_view s, FillRule &v) { return parse_keyword(s, v, fill_rules); }
void format(FillRule v, std::string &out) { format_keyword(v, out, fill_rules); }

int hex_value(char c)
{
    if (is_digit(c)) {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool parse(std::string_view s, Rgb &c)
{
    if ((s.size() != 7 && s.size() != 4) || s[0] != '#') {
        return false;
    }
    bool upper = false;
    bool lower = false;
    int v[6];
    for (std::size_t i = 1; i < s.size(); ++i) {
        v[i - 1] = hex_value(s[i]);
        if (v[i - 1] < 0) {
            return false;
        }
        upper = upper || (s[i] >= 'A' && s[i] <= 'F');
        lower = lower || (s[i] >= 'a' && s[i] <= 'f');
    }
    if (upper && lower) {
        return false;
    }
    if (s.size() == 7) {
        c.r = static_cast<std::uint8_t>(v[0] * 16 + v[1]);
        c.g = static_cast<std::uint8_t>(v[2] * 16 + v[3]);
        c.b = static_cast<std::uint8_t>(v[4] * 16 + v[5]);
        c.form = upper ? ColorForm::upper6 : ColorForm::lower6;
    } else {
        c.r = static_cast<std::uint8_t>(v[0] * 17);
        c.g = static_cast<std::uint8_t>(v[1] * 17);
        c.b = static_cast<std::uint8_t>(v[2] * 17);
        c.form = upper ? ColorForm::upper3 : ColorForm::lower3;
    }
    return true;
}

void format(Rgb const &c, std::string &out)
{
    bool upper = c.form == ColorForm::upper6 || c.form == ColorForm::upper3;
    char const *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    out += '#';
    if (c.form == ColorForm::lower3 || c.form == ColorForm::upper3) {
        for (auto v : {c.r, c.g, c.b}) {
            out += digits[v & 15];
        }
    } else {
        for (auto v : {c.r, c.g, c.b}) {
            out += digits[v >> 4];
            out += digits[v & 15];
        }
    }
}

bool parse(std::string_view s, Paint &p)
{
    PaintKeyword k{};
    if (parse_keyword(s, k, paint_keywords)) {
        p = k;
        return true;
    }
    if (s.starts_with('#')) {
        Rgb c;
        if (!parse(s, c)) {
            return false;
        }
        p = c;
        return true;
    }
    if (s.starts_with("url(") && s.ends_with(')')) {
        p = std::string(s.substr(4, s.size() - 5));
        return true;
    }
    return false;
}

void format(Paint const &p, std::string &out)
{
    if (auto k = std::get_if<PaintKeyword>(&p)) {
        format_keyword(*k, out, paint_keywords);
    } else if (auto c = std::get_if<Rgb>(&p)) {
        format(*c, out);
    } else {
        out += "url(";
        out += std::get<std::string>(p);
        out += ')';
    }
}

bool parse(std::string_view s, std::string &v)
{
    v = s;
    return true;
}

void format(std::string const &v, std::string &out) { out += v; }

void format(Other const &v, std::string &out) { out += v.value; }

// ---------------------------------------------------------------- transforms

bool parse(std::string_view s, Transforms &transforms)
{
    std::vector<TransformOp> ops;
    std::size_t pos = 0;
    skip_separators(s, pos);
    while (pos < s.size()) {
        auto open = s.find('(', pos);
        auto close = s.find(')', pos);
        if (open == std::string_view::npos || close == std::string_view::npos || close < open) {
            return false;
        }
        auto name = s.substr(pos, open - pos);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
            name.remove_suffix(1);
        }
        std::vector<Number> n;
        if (!read_numbers(s.substr(open + 1, close - open - 1), n)) {
            return false;
        }
        if (name == "matrix" && n.size() == 6) {
            ops.emplace_back(Matrix{n[0], n[1], n[2], n[3], n[4], n[5]});
        } else if (name == "translate" && (n.size() == 1 || n.size() == 2)) {
            ops.emplace_back(Translate{n[0], n.size() == 2 ? std::optional(n[1]) : std::nullopt});
        } else if (name == "scale" && (n.size() == 1 || n.size() == 2)) {
            ops.emplace_back(Scale{n[0], n.size() == 2 ? std::optional(n[1]) : std::nullopt});
        } else if (name == "rotate" && (n.size() == 1 || n.size() == 3)) {
            ops.emplace_back(Rotate{n[0], n.size() == 3 ? std::optional(Point{n[1], n[2]}) : std::nullopt});
        } else if (name == "skewX" && n.size() == 1) {
            ops.emplace_back(SkewX{n[0]});
        } else if (name == "skewY" && n.size() == 1) {
            ops.emplace_back(SkewY{n[0]});
        } else {
            return false;
        }
        pos = close + 1;
        skip_separators(s, pos);
    }
    transforms = Transforms(std::move(ops));
    return true;
}

void format_call(std::string &out, char const *name, std::initializer_list<Number const *> args)
{
    out += name;
    out += '(';
    bool first = true;
    for (auto a : args) {
        if (!a) {
            continue;
        }
        if (!first) {
            out += ',';
        }
        first = false;
        format(*a, out);
    }
    out += ')';
}

void format(Transforms const &transforms, std::string &out)
{
    bool first = true;
    for (auto const &op : transforms) {
        if (!first) {
            out += ' ';
        }
        first = false;
        std::visit(
            [&](auto const &o) {
                using O = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<O, Matrix>) {
                    format_call(out, "matrix", {&o.a, &o.b, &o.c, &o.d, &o.e, &o.f});
                } else if constexpr (std::is_same_v<O, Translate>) {
                    format_call(out, "translate", {&o.x, o.y ? &*o.y : nullptr});
                } else if constexpr (std::is_same_v<O, Scale>) {
                    format_call(out, "scale", {&o.x, o.y ? &*o.y : nullptr});
                } else if constexpr (std::is_same_v<O, Rotate>) {
                    format_call(out, "rotate",
                                {&o.angle, o.center ? &o.center->x : nullptr, o.center ? &o.center->y : nullptr});
                } else if constexpr (std::is_same_v<O, SkewX>) {
                    format_call(out, "skewX", {&o.angle});
                } else {
                    format_call(out, "skewY", {&o.angle});
                }
            },
            op);
    }
}

// ---------------------------------------------------------------- path data

/// The command that a segment without a letter repeats after `letter`, or 0
/// if none may follow it so.
char implicit_after(char letter)
{
    switch (letter) {
        case 'M': return 'L';
        case 'm': return 'l';
        case 'Z':
        case 'z':
        case 0: return 0;
        default: return letter;
    }
}

/// The sizes of the groups of a command's numbers: pairs of coordinates, and
/// single numbers.
std::string_view shape_of(char letter)
{
    switch (std::tolower(static_cast<unsigned char>(letter))) {
        case 'm':
        case 'l':
        case 't': return "\2";
        case 'h':
        case 'v': return "\1";
        case 'c': return "\2\2\2";
        case 's':
        case 'q': return "\2\2";
        case 'a': return "\2\1\1\1\2";
        default: return "";
    }
}

bool is_command(char c)
{
    switch (std::tolower(static_cast<unsigned char>(c))) {
        case 'm': case 'l': case 'h': case 'v': case 'c': case 's': case 'q': case 't': case 'a': case 'z':
            return true;
        default:
            return false;
    }
}

std::optional<PathSegment> segment_of(char letter, Number const *n)
{
    bool abs = std::isupper(static_cast<unsigned char>(letter));
    auto flag = [](Number const &f) -> std::optional<bool> {
        if (f.scale != 0 || (f.digits != 0 && f.digits != 1)) {
            return std::nullopt;
        }
        return f.digits == 1;
    };
    switch (std::tolower(static_cast<unsigned char>(letter))) {
        case 'm': return MoveTo{abs, n[0], n[1]};
        case 'l': return LineTo{abs, n[0], n[1]};
        case 'h': return HorizontalLineTo{abs, n[0]};
        case 'v': return VerticalLineTo{abs, n[0]};
        case 'c': return CurveTo{abs, n[0], n[1], n[2], n[3], n[4], n[5]};
        case 's': return SmoothCurveTo{abs, n[0], n[1], n[2], n[3]};
        case 'q': return Quadratic{abs, n[0], n[1], n[2], n[3]};
        case 't': return SmoothQuadratic{abs, n[0], n[1]};
        case 'a': {
            auto large = flag(n[3]);
            auto sweep = flag(n[4]);
            if (!large || !sweep) {
                return std::nullopt;
            }
            return EllipticalArc{abs, n[0], n[1], n[2], *large, *sweep, n[5], n[6]};
        }
        default: return ClosePath{abs};
    }
}

/// A segment's letter, and its numbers in the order the letter takes them.
char letter_of(PathSegment const &seg, std::array<Number, 7> &n)
{
    return std::visit(
        [&](auto const &s) -> char {
            using S = std::decay_t<decltype(s)>;
            char upper = 'Z';
            if constexpr (std::is_same_v<S, MoveTo>) {
                upper = 'M', n[0] = s.x, n[1] = s.y;
            } else if constexpr (std::is_same_v<S, LineTo>) {
                upper = 'L', n[0] = s.x, n[1] = s.y;
            } else if constexpr (std::is_same_v<S, HorizontalLineTo>) {
                upper = 'H', n[0] = s.x;
            } else if constexpr (std::is_same_v<S, VerticalLineTo>) {
                upper = 'V', n[0] = s.y;
            } else if constexpr (std::is_same_v<S, CurveTo>) {
                upper = 'C', n = {s.x1, s.y1, s.x2, s.y2, s.x, s.y, {}};
            } else if constexpr (std::is_same_v<S, SmoothCurveTo>) {
                upper = 'S', n = {s.x2, s.y2, s.x, s.y, {}, {}, {}};
            } else if constexpr (std::is_same_v<S, Quadratic>) {
                upper = 'Q', n = {s.x1, s.y1, s.x, s.y, {}, {}, {}};
            } else if constexpr (std::is_same_v<S, SmoothQuadratic>) {
                upper = 'T', n[0] = s.x, n[1] = s.y;
            } else if constexpr (std::is_same_v<S, EllipticalArc>) {
                upper = 'A';
                n = {s.rx, s.ry, s.x_axis_rotation, Number{s.large_arc ? 1 : 0, 0}, Number{s.sweep ? 1 : 0, 0},
                     s.x, s.y};
            }
            return s.abs ? upper : static_cast<char>(std::tolower(upper));
        },
        seg);
}

/// Reads path data, noting how it separates its numbers and commands.
class PathReader
{
public:
    explicit PathReader(std::string_view s)
        : _s(s)
    {}

    bool read(PathData &out)
    {
        std::vector<PathSegment> segments;
        char prev = 0;
        while (true) {
            int sep = separator();
            if (sep < 0) {
                return false;
            }
            if (_pos >= _s.size()) {
                if (sep != 0) {
                    return false;
                }
                break;
            }
            char letter = _s[_pos];
            bool explicit_letter = is_command(letter);
            if (explicit_letter) {
                if (segments.empty() ? sep != 0 : sep == ',') {
                    return false;
                }
                if (!segments.empty()) {
                    note(_before_command, sep == ' ');
                }
                if (prev && letter == implicit_after(prev)) {
                    note(_repeat, true);
                }
                ++_pos;
            } else {
                letter = implicit_after(prev);
                if (!letter || !gap(sep, _between)) {
                    return false;
                }
                note(_repeat, false);
            }
            std::array<Number, 7> numbers{};
            std::size_t count = 0;
            for (auto group : shape_of(letter)) {
                for (int k = 0; k < group; ++k) {
                    if (count == 0 && explicit_letter) {
                        int s = separator();
                        if (s < 0 || s == ',') {
                            return false;
                        }
                        note(_after_command, s == ' ');
                    } else if (count > 0 && !gap(separator(), k == 1 ? _in_pair : _between)) {
                        return false;
                    }
                    if (!read_number(_s, _pos, numbers[count++])) {
                        return false;
                    }
                }
            }
            auto segment = segment_of(letter, numbers.data());
            if (!segment) {
                return false;
            }
            segments.push_back(*segment);
            prev = letter;
        }
        out.layout = static_cast<std::uint8_t>(
            (_after_command > 0 ? PathLayout::space_after_command : 0) |
            (_in_pair > 0 ? PathLayout::space_in_pair : 0) | (_between > 0 ? PathLayout::space_between : 0) |
            (_glued ? PathLayout::glue_minus : 0) | (_repeat > 0 ? PathLayout::repeat_commands : 0) |
            (_before_command > 0 ? PathLayout::space_before_command : 0));
        out.segments = kladde::Vector<PathSegment>(std::move(segments));
        return true;
    }

private:
    /// The separator at the read position: 0 for none, ' ' or ',', or -1 for
    /// one the layout cannot express.
    int separator()
    {
        if (_pos >= _s.size()) {
            return 0;
        }
        char c = _s[_pos];
        if (c == ' ' || c == ',') {
            ++_pos;
            if (_pos < _s.size() && (_s[_pos] == ',' || std::isspace(static_cast<unsigned char>(_s[_pos])))) {
                return -1;
            }
            return c;
        }
        return std::isspace(static_cast<unsigned char>(c)) ? -1 : 0;
    }

    /// Notes the separator `sep` between two numbers, of the kind `slot`
    /// counts; false if it cannot be one.
    bool gap(int sep, int &slot)
    {
        if (sep < 0) {
            return false;
        }
        if (sep == 0) {
            if (_pos >= _s.size() || _s[_pos] != '-') {
                return false;
            }
            _glued = true;
            return true;
        }
        note(slot, sep == ' ');
        return true;
    }

    static void note(int &slot, bool yes)
    {
        if (slot < 0) {
            slot = yes ? 1 : 0;
        }
    }

    std::string_view _s;
    std::size_t _pos = 0;
    int _after_command = -1;
    int _in_pair = -1;
    int _between = -1;
    int _before_command = -1;
    int _repeat = -1;
    bool _glued = false;
};

bool parse(std::string_view s, PathData &d) { return PathReader(s).read(d); }

void format(PathData const &d, std::string &out)
{
    auto has = [&](std::uint8_t bit) { return (d.layout & bit) != 0; };
    char in_pair = has(PathLayout::space_in_pair) ? ' ' : ',';
    char between = has(PathLayout::space_between) ? ' ' : ',';
    bool glue = has(PathLayout::glue_minus);
    char prev = 0;
    bool first = true;
    for (auto const &segment : d.segments) {
        std::array<Number, 7> n;
        char letter = letter_of(segment, n);
        bool write = first || has(PathLayout::repeat_commands) || letter != implicit_after(prev);
        if (write) {
            if (!first && has(PathLayout::space_before_command)) {
                out += ' ';
            }
            out += letter;
        }
        std::size_t count = 0;
        for (auto group : shape_of(letter)) {
            for (int k = 0; k < group; ++k) {
                auto const &number = n[count];
                if (count == 0 && write) {
                    if (has(PathLayout::space_after_command)) {
                        out += ' ';
                    }
                } else if (!(glue && number.digits < 0)) {
                    out += k == 1 ? in_pair : between;
                }
                format(number, out);
                ++count;
            }
        }
        prev = letter;
        first = false;
    }
}

// ---------------------------------------------------------------- typed attributes

bool parse(std::string_view s, Declarations &decls);
void format(Declarations const &decls, std::string &out);

/// Alternative `I` of `V` holding `value`, if it parses as one and formats
/// back exactly.
template <class V, std::size_t I>
std::optional<V> typed(std::string_view value)
{
    using T = std::variant_alternative_t<I, V>;
    T t{};
    if (!parse(value, t)) {
        return std::nullopt;
    }
    std::string text;
    text.reserve(value.size());
    format(t, text);
    if (text != value) {
        return std::nullopt;
    }
    return V(std::in_place_index<I>, std::move(t));
}

template <class V>
using Parser = std::optional<V> (*)(std::string_view);

std::unordered_map<std::string_view, Parser<Attr>> const &attribute_parsers()
{
    static std::unordered_map<std::string_view, Parser<Attr>> const parsers = {
#define INKSCAPE_KLADDE_ENTRY_(id, name, T) {name, &typed<Attr, std::size_t(AttrKind::id)>},
        INKSCAPE_KLADDE_PROPERTIES(INKSCAPE_KLADDE_ENTRY_) INKSCAPE_KLADDE_ATTRIBUTES(INKSCAPE_KLADDE_ENTRY_)
#undef INKSCAPE_KLADDE_ENTRY_
    };
    return parsers;
}

std::unordered_map<std::string_view, Parser<Declaration>> const &declaration_parsers()
{
    static std::unordered_map<std::string_view, Parser<Declaration>> const parsers = {
#define INKSCAPE_KLADDE_ENTRY_(id, name, T) {name, &typed<Declaration, std::size_t(DeclarationKind::id)>},
        INKSCAPE_KLADDE_PROPERTIES(INKSCAPE_KLADDE_ENTRY_)
#undef INKSCAPE_KLADDE_ENTRY_
    };
    return parsers;
}

constexpr std::string_view attribute_names[] = {
#define INKSCAPE_KLADDE_NAME_(id, name, T) name,
    INKSCAPE_KLADDE_PROPERTIES(INKSCAPE_KLADDE_NAME_) INKSCAPE_KLADDE_ATTRIBUTES(INKSCAPE_KLADDE_NAME_)
#undef INKSCAPE_KLADDE_NAME_
};

Declaration declaration_of(std::string_view name, std::string_view value)
{
    auto const &parsers = declaration_parsers();
    if (auto it = parsers.find(name); it != parsers.end()) {
        if (auto d = it->second(value)) {
            return std::move(*d);
        }
    }
    return Other{std::string(name), std::string(value)};
}

std::string_view declaration_name(Declaration const &d)
{
    if (auto other = std::get_if<Other>(&d)) {
        return other->name;
    }
    return attribute_names[d.index()];
}

bool parse(std::string_view s, Declarations &decls)
{
    std::vector<Declaration> items;
    if (!s.empty()) {
        std::size_t start = 0;
        char quote = 0;
        int depth = 0;
        for (std::size_t i = 0; i <= s.size(); ++i) {
            if (i == s.size() || (s[i] == ';' && !quote && depth == 0)) {
                auto part = s.substr(start, i - start);
                auto colon = part.find(':');
                if (colon == std::string_view::npos) {
                    return false;
                }
                items.push_back(declaration_of(part.substr(0, colon), part.substr(colon + 1)));
                start = i + 1;
            } else if (quote) {
                if (s[i] == quote) {
                    quote = 0;
                }
            } else if (s[i] == '"' || s[i] == '\'') {
                quote = s[i];
            } else if (s[i] == '(') {
                ++depth;
            } else if (s[i] == ')') {
                --depth;
            }
        }
        if (quote || depth != 0) {
            return false;
        }
    }
    decls = Declarations(std::move(items));
    return true;
}

void format(Declarations const &decls, std::string &out)
{
    bool first = true;
    for (auto const &d : decls) {
        if (!first) {
            out += ';';
        }
        first = false;
        out += declaration_name(d);
        out += ':';
        std::visit([&](auto const &v) { format(v, out); }, d);
    }
}

// ---------------------------------------------------------------- element names

constexpr std::string_view tag_names[] = {
    "svg",     "g",          "defs",     "symbol",         "use",            "path",     "rect",
    "circle",  "ellipse",    "line",     "polyline",       "polygon",        "text",     "tspan",
    "textPath", "linearGradient", "radialGradient", "stop", "clipPath",       "mask",     "pattern",
    "marker",  "image",      "style",    "title",          "desc",           "metadata", "filter",
    "a",       "script",     "foreignObject",
};
static_assert(std::size(tag_names) == std::size_t(Tag::foreignObject) + 1);

constexpr std::string_view svg_prefix = "svg:";

} // namespace

Attr attribute_of(std::string_view name, std::string_view value)
{
    auto const &parsers = attribute_parsers();
    if (auto it = parsers.find(name); it != parsers.end()) {
        if (auto a = it->second(value)) {
            return std::move(*a);
        }
    }
    return Other{std::string(name), std::string(value)};
}

std::string_view attribute_name(Attr const &attr)
{
    if (auto other = std::get_if<Other>(&attr)) {
        return other->name;
    }
    return attribute_names[attr.index()];
}

std::string attribute_value(Attr const &attr)
{
    std::string out;
    std::visit([&](auto const &v) { format(v, out); }, attr);
    return out;
}

bool is_typed(Attr const &attr) { return !std::holds_alternative<Other>(attr); }

bool types_attribute(std::string_view name) { return attribute_parsers().contains(name); }

Name name_of(std::string_view name)
{
    static std::unordered_map<std::string_view, Tag> const tags = [] {
        std::unordered_map<std::string_view, Tag> map;
        for (std::size_t i = 0; i < std::size(tag_names); ++i) {
            map.emplace(tag_names[i], static_cast<Tag>(i));
        }
        return map;
    }();
    if (name.starts_with(svg_prefix)) {
        if (auto it = tags.find(name.substr(svg_prefix.size())); it != tags.end()) {
            return it->second;
        }
    }
    return std::string(name);
}

std::string qualified_name(Name const &name)
{
    if (auto tag = std::get_if<Tag>(&name)) {
        return std::string(svg_prefix) + std::string(tag_names[static_cast<std::size_t>(*tag)]);
    }
    return std::get<std::string>(name);
}

} // namespace Inkscape::Kladde
