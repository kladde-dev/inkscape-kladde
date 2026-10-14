// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * kladde's typed SVG schema: Inkscape's XML tree as durable values, with the
 * attributes SVG defines held as typed values.
 *
 * A document is an element without a name, whose attributes are the XML
 * declaration's and whose children are the root element and the comments and
 * processing instructions around it, as in Inkscape's tree.
 *
 * An element's name is a `Tag` for the SVG elements the schema knows, and
 * Inkscape's qualified name (`sodipodi:namedview`) otherwise. Its attributes
 * are an ordered list of `Attr`s, in Inkscape's order: an attribute the
 * schema knows (`fill`, `d`, `transform`, `style`, ...) is held as a typed
 * value, and any other, or one whose value does not reproduce its text
 * exactly when formatted again, as `Other`, verbatim. So a document reads
 * back byte for byte as Inkscape held it; see svg-values.h for the parsing
 * and formatting.
 *
 * Numbers are decimals, `digits / 10^scale`, rather than binary floats, so
 * that `1.250000` and `1.25` are different values that each format back as
 * they were written.
 */
#ifndef INKSCAPE_KLADDE_SCHEMA_H
#define INKSCAPE_KLADDE_SCHEMA_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <kladde/kladde.hpp>

namespace Inkscape::Kladde {

// ---------------------------------------------------------------- numbers and lengths

/// A decimal number as written: `-12.50` is `{-1250, 2}`.
struct Number
{
    std::int32_t digits = 0;
    /// The number of digits after the decimal point.
    std::uint8_t scale = 0;
    friend bool operator==(Number const &, Number const &) = default;
};
KLADDE_STRUCT(Number, digits, scale)

enum class LengthUnit : std::uint8_t { none, em, ex, px, in, cm, mm, pt, pc, percent };
KLADDE_ENUM(LengthUnit, none, em, ex, px, in, cm, mm, pt, pc, percent)

/// A length or coordinate: a number and its unit.
struct Length
{
    Number value;
    LengthUnit unit = LengthUnit::none;
    friend bool operator==(Length const &, Length const &) = default;
};
KLADDE_STRUCT(Length, value, unit)

/// A list of lengths, such as a dash array, written with commas.
using Lengths = kladde::Vector<Length>;

/// A point of a `points` list, or the center of a rotation.
struct Point
{
    Number x;
    Number y;
    friend bool operator==(Point const &, Point const &) = default;
};
KLADDE_STRUCT(Point, x, y)

/// A `points` list, written `x,y x,y ...`.
using PointList = kladde::Vector<Point>;

struct ViewBox
{
    Number x;
    Number y;
    Number width;
    Number height;
    friend bool operator==(ViewBox const &, ViewBox const &) = default;
};
KLADDE_STRUCT(ViewBox, x, y, width, height)

// ---------------------------------------------------------------- paint

/// How a color was written: six or three hexadecimal digits, in lower or
/// upper case.
enum class ColorForm : std::uint8_t { lower6, upper6, lower3, upper3 };
KLADDE_ENUM(ColorForm, lower6, upper6, lower3, upper3)

/// An sRGB color written in hexadecimal.
struct Rgb
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    ColorForm form = ColorForm::lower6;
    friend bool operator==(Rgb const &, Rgb const &) = default;
};
KLADDE_STRUCT(Rgb, r, g, b, form)

enum class PaintKeyword : std::uint8_t { none, currentColor, inherit };
KLADDE_ENUM(PaintKeyword, none, currentColor, inherit)

/// The value of `fill` or `stroke`: a keyword, a color, or a reference to a
/// paint server by its IRI (`#gradient` for `url(#gradient)`).
using Paint = std::variant<PaintKeyword, Rgb, std::string>;

enum class LineCap : std::uint8_t { butt, round, square };
KLADDE_ENUM(LineCap, butt, round, square)

enum class LineJoin : std::uint8_t { miter, miter_clip, round, bevel, arcs };
KLADDE_ENUM(LineJoin, miter, miter_clip, round, bevel, arcs)

enum class FillRule : std::uint8_t { nonzero, evenodd };
KLADDE_ENUM(FillRule, nonzero, evenodd)

// ---------------------------------------------------------------- transforms

struct Matrix
{
    Number a, b, c, d, e, f;
    friend bool operator==(Matrix const &, Matrix const &) = default;
};
KLADDE_STRUCT(Matrix, a, b, c, d, e, f)

struct Translate
{
    Number x;
    /// Absent if the source wrote `translate(x)`.
    std::optional<Number> y;
    friend bool operator==(Translate const &, Translate const &) = default;
};
KLADDE_STRUCT(Translate, x, y)

struct Scale
{
    Number x;
    std::optional<Number> y;
    friend bool operator==(Scale const &, Scale const &) = default;
};
KLADDE_STRUCT(Scale, x, y)

struct Rotate
{
    Number angle;
    std::optional<Point> center;
    friend bool operator==(Rotate const &, Rotate const &) = default;
};
KLADDE_STRUCT(Rotate, angle, center)

struct SkewX
{
    Number angle;
    friend bool operator==(SkewX const &, SkewX const &) = default;
};
KLADDE_STRUCT(SkewX, angle)

struct SkewY
{
    Number angle;
    friend bool operator==(SkewY const &, SkewY const &) = default;
};
KLADDE_STRUCT(SkewY, angle)

using TransformOp = std::variant<Matrix, Translate, Scale, Rotate, SkewX, SkewY>;

/// A transform list, written `op(a,b,...)` with commas, ops separated by a
/// space.
using Transforms = kladde::Vector<TransformOp>;

// ---------------------------------------------------------------- path data

struct MoveTo
{
    bool abs = true;
    Number x, y;
    friend bool operator==(MoveTo const &, MoveTo const &) = default;
};
KLADDE_STRUCT(MoveTo, abs, x, y)

struct LineTo
{
    bool abs = true;
    Number x, y;
    friend bool operator==(LineTo const &, LineTo const &) = default;
};
KLADDE_STRUCT(LineTo, abs, x, y)

struct HorizontalLineTo
{
    bool abs = true;
    Number x;
    friend bool operator==(HorizontalLineTo const &, HorizontalLineTo const &) = default;
};
KLADDE_STRUCT(HorizontalLineTo, abs, x)

struct VerticalLineTo
{
    bool abs = true;
    Number y;
    friend bool operator==(VerticalLineTo const &, VerticalLineTo const &) = default;
};
KLADDE_STRUCT(VerticalLineTo, abs, y)

struct CurveTo
{
    bool abs = true;
    Number x1, y1, x2, y2, x, y;
    friend bool operator==(CurveTo const &, CurveTo const &) = default;
};
KLADDE_STRUCT(CurveTo, abs, x1, y1, x2, y2, x, y)

struct SmoothCurveTo
{
    bool abs = true;
    Number x2, y2, x, y;
    friend bool operator==(SmoothCurveTo const &, SmoothCurveTo const &) = default;
};
KLADDE_STRUCT(SmoothCurveTo, abs, x2, y2, x, y)

struct Quadratic
{
    bool abs = true;
    Number x1, y1, x, y;
    friend bool operator==(Quadratic const &, Quadratic const &) = default;
};
KLADDE_STRUCT(Quadratic, abs, x1, y1, x, y)

struct SmoothQuadratic
{
    bool abs = true;
    Number x, y;
    friend bool operator==(SmoothQuadratic const &, SmoothQuadratic const &) = default;
};
KLADDE_STRUCT(SmoothQuadratic, abs, x, y)

struct EllipticalArc
{
    bool abs = true;
    Number rx, ry, x_axis_rotation;
    bool large_arc = false;
    bool sweep = false;
    Number x, y;
    friend bool operator==(EllipticalArc const &, EllipticalArc const &) = default;
};
KLADDE_STRUCT(EllipticalArc, abs, rx, ry, x_axis_rotation, large_arc, sweep, x, y)

struct ClosePath
{
    bool abs = true;
    friend bool operator==(ClosePath const &, ClosePath const &) = default;
};
KLADDE_STRUCT(ClosePath, abs)

/// One segment of path data, as the `d` attribute writes it: absolute or
/// relative, and one of the ten commands.
using PathSegment = std::variant<MoveTo, LineTo, HorizontalLineTo, VerticalLineTo, CurveTo, SmoothCurveTo,
                                 Quadratic, SmoothQuadratic, EllipticalArc, ClosePath>;

/// The bits of `PathData::layout`: how the source separated the numbers and
/// commands of its path data, which writers differ in (`m 1,2 c ...` from
/// Inkscape, `m1,2c...` from minifiers).
namespace PathLayout {
/// A space after a command letter.
inline constexpr std::uint8_t space_after_command = 1;
/// A space, not a comma, between the x and y of a pair.
inline constexpr std::uint8_t space_in_pair = 2;
/// A space, not a comma, between the pairs and numbers of segments.
inline constexpr std::uint8_t space_between = 4;
/// No separator before a negative number, but after a command letter.
inline constexpr std::uint8_t glue_minus = 8;
/// Every segment has its command letter, even where it repeats the previous
/// one's.
inline constexpr std::uint8_t repeat_commands = 16;
/// A space before a command letter.
inline constexpr std::uint8_t space_before_command = 32;
} // namespace PathLayout

/// Path data: its segments, and how they were written.
struct PathData
{
    std::uint8_t layout = 0;
    kladde::Vector<PathSegment> segments{};
    friend bool operator==(PathData const &, PathData const &) = default;
};
KLADDE_STRUCT(PathData, layout, segments)

// ---------------------------------------------------------------- attributes

/// An attribute, or a declaration of a `style`, kept verbatim: one the
/// schema does not type, or whose value would not format back as written.
struct Other
{
    std::string name;
    std::string value;
    friend bool operator==(Other const &, Other const &) = default;
};
KLADDE_STRUCT(Other, name, value)

/// The presentation properties the schema types, which are attributes and
/// `style` declarations alike: the variant's name, the property's name, and
/// the type of its value.
#define INKSCAPE_KLADDE_PROPERTIES(X)                                                                                  \
    X(Fill, "fill", Paint)                                                                                             \
    X(Stroke, "stroke", Paint)                                                                                         \
    X(Opacity, "opacity", Number)                                                                                      \
    X(FillOpacity, "fill-opacity", Number)                                                                             \
    X(StrokeOpacity, "stroke-opacity", Number)                                                                         \
    X(StopOpacity, "stop-opacity", Number)                                                                             \
    X(StrokeWidth, "stroke-width", Length)                                                                             \
    X(StrokeDashoffset, "stroke-dashoffset", Length)                                                                   \
    X(StrokeMiterlimit, "stroke-miterlimit", Number)                                                                   \
    X(StrokeDasharray, "stroke-dasharray", Lengths)                                                                    \
    X(StrokeLinecap, "stroke-linecap", LineCap)                                                                        \
    X(StrokeLinejoin, "stroke-linejoin", LineJoin)                                                                     \
    X(FillRule, "fill-rule", FillRule)                                                                                 \
    X(ClipRule, "clip-rule", FillRule)                                                                                 \
    X(StopColor, "stop-color", Rgb)                                                                                    \
    X(Color, "color", Rgb)                                                                                             \
    X(FontSize, "font-size", Length)                                                                                   \
    X(FontFamily, "font-family", std::string)

/// A declaration of a `style` attribute.
using Declaration = std::variant<
#define INKSCAPE_KLADDE_TYPE_(id, name, T) T,
    INKSCAPE_KLADDE_PROPERTIES(INKSCAPE_KLADDE_TYPE_)
#undef INKSCAPE_KLADDE_TYPE_
        Other>;

/// A `style` attribute's declarations, written `name:value;name:value`.
using Declarations = kladde::Vector<Declaration>;

/// The attributes other than presentation properties that the schema types.
#define INKSCAPE_KLADDE_ATTRIBUTES(M)                                                                                  \
    M(Id, "id", std::string)                                                                                           \
    M(Class, "class", std::string)                                                                                     \
    M(Href, "href", std::string)                                                                                       \
    M(XlinkHref, "xlink:href", std::string)                                                                            \
    M(ViewBox, "viewBox", ViewBox)                                                                                     \
    M(X, "x", Length)                                                                                                  \
    M(Y, "y", Length)                                                                                                  \
    M(Width, "width", Length)                                                                                          \
    M(Height, "height", Length)                                                                                        \
    M(Rx, "rx", Length)                                                                                                \
    M(Ry, "ry", Length)                                                                                                \
    M(Cx, "cx", Length)                                                                                                \
    M(Cy, "cy", Length)                                                                                                \
    M(R, "r", Length)                                                                                                  \
    M(X1, "x1", Length)                                                                                                \
    M(Y1, "y1", Length)                                                                                                \
    M(X2, "x2", Length)                                                                                                \
    M(Y2, "y2", Length)                                                                                                \
    M(Offset, "offset", Length)                                                                                        \
    M(D, "d", PathData)                                                                                                \
    M(Points, "points", PointList)                                                                                     \
    M(Transform, "transform", Transforms)                                                                              \
    M(GradientTransform, "gradientTransform", Transforms)                                                              \
    M(PatternTransform, "patternTransform", Transforms)                                                                \
    M(Style, "style", Declarations)

/// An attribute: typed where the schema knows it and its value formats back
/// as written, `Other` otherwise.
using Attr = std::variant<
#define INKSCAPE_KLADDE_TYPE_(id, name, T) T,
    INKSCAPE_KLADDE_PROPERTIES(INKSCAPE_KLADDE_TYPE_) INKSCAPE_KLADDE_ATTRIBUTES(INKSCAPE_KLADDE_TYPE_)
#undef INKSCAPE_KLADDE_TYPE_
        Other>;

/// The alternatives of `Attr`, by name.
enum class AttrKind : std::size_t {
#define INKSCAPE_KLADDE_ENUM_(id, name, T) id,
    INKSCAPE_KLADDE_PROPERTIES(INKSCAPE_KLADDE_ENUM_) INKSCAPE_KLADDE_ATTRIBUTES(INKSCAPE_KLADDE_ENUM_)
#undef INKSCAPE_KLADDE_ENUM_
        Other
};

/// The alternatives of `Declaration`, by name.
enum class DeclarationKind : std::size_t {
#define INKSCAPE_KLADDE_ENUM_(id, name, T) id,
    INKSCAPE_KLADDE_PROPERTIES(INKSCAPE_KLADDE_ENUM_)
#undef INKSCAPE_KLADDE_ENUM_
        Other
};

// ---------------------------------------------------------------- the tree

/// The SVG elements the schema names; Inkscape calls them `svg:<tag>`.
enum class Tag : std::uint8_t {
    svg, g, defs, symbol, use, path, rect, circle, ellipse, line, polyline, polygon, text, tspan, textPath,
    linearGradient, radialGradient, stop, clipPath, mask, pattern, marker, image, style, title, desc, metadata,
    filter, a, script, foreignObject
};
KLADDE_ENUM(Tag, svg, g, defs, symbol, use, path, rect, circle, ellipse, line, polyline, polygon, text, tspan,
            textPath, linearGradient, radialGradient, stop, clipPath, mask, pattern, marker, image, style, title,
            desc, metadata, filter, a, script, foreignObject)

/// An element's name: an SVG tag, or any other qualified name.
using Name = std::variant<Tag, std::string>;

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
    Name name;
    /// In their order, as Inkscape keeps them.
    kladde::Vector<Attr> attributes{};
    /// A list rather than a vector, since Inkscape holds pointers to nodes
    /// for as long as it likes, and each needs a `kladde::Ref`.
    kladde::List<Node> children{};
    friend bool operator==(Element const &, Element const &) = default;
};
KLADDE_STRUCT(Element, name, attributes, children)

} // namespace Inkscape::Kladde

KLADDE_VARIANT_NAMED(Inkscape::Kladde::Paint, "Paint", (0, "Keyword"), (1, "Color"), (2, "Url"))
KLADDE_VARIANT(Inkscape::Kladde::TransformOp, "TransformOp", 0, 1, 2, 3, 4, 5)
KLADDE_VARIANT(Inkscape::Kladde::PathSegment, "PathSegment", 0, 1, 2, 3, 4, 5, 6, 7, 8, 9)
KLADDE_VARIANT_NAMED(Inkscape::Kladde::Name, "Name", (0, "Svg"), (1, "Other"))
KLADDE_VARIANT(Inkscape::Kladde::Node, "Node", 0, 1, 2, 3)

/// `Attr` and `Declaration` are named after the attributes they hold, and
/// numbered in the order of the lists above, which a file's schema pins: add
/// to them only at the end, before `Other`, in a new version of the schema.
template <>
struct kladde::variant_info<Inkscape::Kladde::Attr>
{
    static constexpr std::string_view name = "Attr";
    static constexpr std::string_view names[] = {
#define INKSCAPE_KLADDE_NAME_(id, name, T) #id,
        INKSCAPE_KLADDE_PROPERTIES(INKSCAPE_KLADDE_NAME_) INKSCAPE_KLADDE_ATTRIBUTES(INKSCAPE_KLADDE_NAME_)
#undef INKSCAPE_KLADDE_NAME_
            "Other"};
    static_assert(std::size(names) == std::variant_size_v<Inkscape::Kladde::Attr>);
    static constexpr std::uint64_t discriminant(std::size_t index) { return index; }
    static constexpr std::string_view variant_name(std::size_t index) { return names[index]; }
};

template <>
struct kladde::variant_info<Inkscape::Kladde::Declaration>
{
    static constexpr std::string_view name = "Declaration";
    static constexpr std::string_view names[] = {
#define INKSCAPE_KLADDE_NAME_(id, name, T) #id,
        INKSCAPE_KLADDE_PROPERTIES(INKSCAPE_KLADDE_NAME_)
#undef INKSCAPE_KLADDE_NAME_
            "Other"};
    static_assert(std::size(names) == std::variant_size_v<Inkscape::Kladde::Declaration>);
    static constexpr std::uint64_t discriminant(std::size_t index) { return index; }
    static constexpr std::string_view variant_name(std::size_t index) { return names[index]; }
};

#endif // INKSCAPE_KLADDE_SCHEMA_H
