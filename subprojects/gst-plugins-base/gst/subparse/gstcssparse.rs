/* GStreamer CSS parser
 * Copyright (c) 2025 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

//! Pure Rust CSS parser for GStreamer WebVTT subtitles.
//!
//! Hand-written parser (no external crate dependencies) that replaces the
//! libcss C dependency.  The entry point is the C FFI at the bottom of this
//! file; the C side calls:
//!
//!   1. `gst_cssparse_new`             — allocate parser state
//!   2. `gst_cssparse_parse`           — feed CSS text, apply matching rules
//!   3. `gst_cssparse_set_cue_id`      — set the active cue for selector matching
//!   4. `gst_cssparse_to_pango_markup` — convert cue text → Pango markup
//!   5. `gst_cssparse_free`            — release
//!
//! Supports WebVTT `::cue`, `#id`, `.class` selectors and a subset of CSS
//! properties: color, background-color, font-size, font-family, font-weight,
//! padding.
//!
//! Positioning metadata is carried via GstCustomMeta ("GstWebVTTCueMeta")
//! attached to the output buffer — not embedded in the Pango markup.

use std::collections::HashMap;
use std::ffi::{c_char, c_int, c_void, CStr};

// ---------------------------------------------------------------------------
// External C functions (glib)
// ---------------------------------------------------------------------------

extern "C" {
    fn g_malloc(n_bytes: usize) -> *mut c_void;
}

/// Allocate a C string via `g_malloc` so the caller can free it with `g_free`.
/// Copies `s` into a newly-allocated NUL-terminated buffer.
fn string_to_c(s: &str) -> *mut c_char {
    unsafe {
        let ptr = g_malloc(s.len() + 1) as *mut u8;
        if ptr.is_null() {
            return std::ptr::null_mut();
        }
        std::ptr::copy_nonoverlapping(s.as_ptr(), ptr, s.len());
        *ptr.add(s.len()) = 0;
        ptr as *mut c_char
    }
}

// ---------------------------------------------------------------------------
// CSS Value Types
// ---------------------------------------------------------------------------

/// CSS length unit — only the three units relevant to WebVTT.
#[derive(Debug, Clone, Copy, PartialEq)]
enum CssUnit {
    Px,  // absolute pixels
    Pct, // percentage of parent / viewport
    Em,  // relative to current font size
}

/// Simplified font-weight (WebVTT only needs normal vs bold).
#[derive(Debug, Clone, Copy, PartialEq)]
enum FontWeight {
    Normal,
    Bold,
}

/// Parsed CSS property value.
#[derive(Debug, Clone, PartialEq)]
enum CssValue {
    Color(u32), // ARGB
    Length(f32, CssUnit),
    FontFamily(String),
    FontWeight(FontWeight),
}

// ---------------------------------------------------------------------------
// CSS Selector
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq)]
enum CssSelector {
    /// `::cue` or `cue` — matches all cues
    Universal,
    /// `::cue(#id)` or `#id` — matches specific cue by ID
    Id(String),
    /// `::cue(.class)` or `.class` — matches cue text with class
    Class(String),
    /// Element selector like `cue`, `b`, `i`
    Element(String),
}

// ---------------------------------------------------------------------------
// CSS Rule
// ---------------------------------------------------------------------------

/// A single CSS rule: selector + list of property→value declarations.
#[derive(Debug, Clone)]
struct CssRule {
    selector: CssSelector,
    declarations: Vec<(String, CssValue)>,
}

// ---------------------------------------------------------------------------
// Main Parser State
// ---------------------------------------------------------------------------

/// Opaque parser state exposed to C via `*mut GstCssParse`.
///
/// Lifecycle:
///   new() → [set_cue_id]* → parse() → to_pango_markup() → reinit() → … → free()
///
/// `rules` persist across `reinit()` so STYLE blocks parsed once are
/// reused for every subsequent cue.  All other fields are reset per cue.
pub struct GstCssParse {
    /* --- persistent state (survives reinit) --- */
    rules: Vec<CssRule>, // accumulated CSS rules from STYLE blocks

    /* --- per-cue state (reset by reinit) --- */
    current_cue_id: Option<String>, // cue ID for #id selector matching

    // Computed CSS properties (applied from matching rules)
    color: u32,            // foreground ARGB
    background_color: u32, // background ARGB
    font_size: f32,
    font_size_unit: CssUnit,
    font_family: String,
    font_weight: FontWeight,
    padding: f32,
    padding_unit: CssUnit,

    // Video dimensions for font size scaling
    video_height: i32, // video frame height (default 1080)
}

impl GstCssParse {
    fn new() -> Self {
        GstCssParse {
            rules: Vec::new(),
            current_cue_id: None,
            color: 0,
            background_color: 0,
            font_size: 0.0,
            font_size_unit: CssUnit::Px,
            font_family: "sans-serif".to_string(),
            font_weight: FontWeight::Normal,
            padding: 0.0,
            padding_unit: CssUnit::Px,
            video_height: 1080,
        }
    }

    /// Reset per-cue computed properties while preserving accumulated CSS rules.
    /// Called before processing each new cue.
    fn reinit(&mut self) {
        self.current_cue_id = None;
        self.color = 0;
        self.background_color = 0;
        self.font_size = 0.0;
        self.font_size_unit = CssUnit::Px;
        self.font_family = "sans-serif".to_string();
        self.font_weight = FontWeight::Normal;
        self.padding = 0.0;
        self.padding_unit = CssUnit::Px;
    }

    /// Parse CSS text and apply matching rules for the current cue.
    fn parse(&mut self, css_data: &str) -> i32 {
        // Parse new CSS rules if data is provided
        if !css_data.is_empty() {
            let new_rules = parse_css_rules(css_data);
            self.rules.extend(new_rules);
        }

        // Apply rules that match at the cue level
        self.apply_cue_level_rules();

        0 // success
    }

    /// Apply CSS rules matching the current cue (Universal and ID selectors).
    fn apply_cue_level_rules(&mut self) {
        // Apply rules in order (later rules override earlier ones)
        for rule in self.rules.clone() {
            let matches = match &rule.selector {
                CssSelector::Universal => true,
                CssSelector::Element(name) => name == "cue",
                CssSelector::Id(id) => self
                    .current_cue_id
                    .as_ref()
                    .map_or(false, |cue_id| cue_id.eq_ignore_ascii_case(id)),
                CssSelector::Class(_) => false, // class selectors match in to_pango_markup
            };

            if matches {
                for (prop, value) in &rule.declarations {
                    self.apply_declaration(prop, value);
                }
            }
        }
    }

    /// Apply a single CSS declaration to the computed properties.
    fn apply_declaration(&mut self, prop: &str, value: &CssValue) {
        match prop {
            "color" => {
                if let CssValue::Color(c) = value {
                    self.color = *c;
                }
            }
            "background-color" => {
                if let CssValue::Color(c) = value {
                    self.background_color = *c;
                }
            }
            "font-size" => {
                if let CssValue::Length(v, u) = value {
                    self.font_size = *v;
                    self.font_size_unit = *u;
                }
            }
            "font-family" => {
                if let CssValue::FontFamily(f) = value {
                    self.font_family = f.clone();
                }
            }
            "font-weight" => {
                if let CssValue::FontWeight(w) = value {
                    self.font_weight = *w;
                }
            }
            "padding" | "padding-top" => {
                if let CssValue::Length(v, u) = value {
                    self.padding = *v;
                    self.padding_unit = *u;
                }
            }
            _ => {}
        }
    }

    /// Look up CSS declarations for a given class name.
    fn get_class_declarations(&self, class_name: &str) -> Vec<(String, CssValue)> {
        let mut result = Vec::new();
        for rule in &self.rules {
            let matches = match &rule.selector {
                CssSelector::Class(cls) => cls.eq_ignore_ascii_case(class_name),
                _ => false,
            };
            if matches {
                result.extend(rule.declarations.clone());
            }
        }
        result
    }

    /// Convert cue text (WebVTT HTML-like markup) to Pango markup.
    ///
    /// Steps:
    ///   1. Wrap text in a `<span>` with cue-level CSS properties.
    ///   2. Walk the cue body, translating `<b>`,`<i>`,`<u>`,`<c.class>`
    ///      tags into the Pango equivalents.
    ///
    /// Positioning metadata is carried separately via GstCustomMeta
    /// ("GstWebVTTCueMeta") on the buffer — not embedded in the markup.
    fn to_pango_markup(&self, text: &str) -> String {
        let mut markup = String::new();

        // WebVTT spec §7.1: default line height is 5.33% of viewport height.
        // Cap font sizes at 30 % of that to avoid oversized text.
        let ref_line_height = 0.0533 * self.video_height as f32;
        let max_font_size = ref_line_height * 0.3;

        let has_cue_specific_padding = self.padding > 0.0 && self.padding_unit == CssUnit::Px;

        // Build global ::cue style span attributes
        let span_attrs = self.build_span_attrs(
            self.color,
            self.background_color,
            self.font_size,
            self.font_size_unit,
            self.font_weight,
            &self.font_family,
            if has_cue_specific_padding {
                Some(self.padding)
            } else {
                None
            },
            ref_line_height,
            max_font_size,
        );

        let has_styles = !span_attrs.is_empty();
        if has_styles {
            markup.push_str(&format!("<span {}>", span_attrs));
        }

        // Parse cue text and generate inner markup
        self.process_cue_text(text, &mut markup, ref_line_height, max_font_size);

        if has_styles {
            markup.push_str("</span>");
        }

        markup
    }

    /// Build Pango span attributes from CSS properties.
    fn build_span_attrs(
        &self,
        color: u32,
        bg_color: u32,
        font_size: f32,
        font_size_unit: CssUnit,
        font_weight: FontWeight,
        font_family: &str,
        padding: Option<f32>,
        ref_line_height: f32,
        max_font_size: f32,
    ) -> String {
        let mut attrs = String::new();
        let mut applied: HashMap<&str, bool> = HashMap::new();

        // Color
        if color != 0 {
            let (a, r, g, b) = argb_components(color);
            attrs.push_str(&format!(
                "foreground=\"#{:02X}{:02X}{:02X}{:02X}\" ",
                r, g, b, a
            ));
            applied.insert("foreground", true);
        }

        // Background color
        if bg_color != 0 {
            let (a, r, g, b) = argb_components(bg_color);
            attrs.push_str(&format!(
                "background=\"#{:02X}{:02X}{:02X}{:02X}\" ",
                r, g, b, a
            ));
            applied.insert("background", true);
        }

        // Font size
        let size = if font_size > 0.0 {
            cap_font_size(font_size, font_size_unit, ref_line_height, max_font_size)
        } else {
            16.0 // default
        };
        attrs.push_str(&format!("size=\"{}\" ", (size * 1024.0) as i32));

        // Font weight
        if font_weight == FontWeight::Bold {
            attrs.push_str("weight=\"bold\" ");
        }

        // Font family
        if !font_family.is_empty() {
            attrs.push_str(&format!("font=\"{}\" ", font_family));
        }

        // Padding as rise
        if let Some(pad) = padding {
            let rise = (pad * 1024.0) as i32;
            attrs.push_str(&format!("rise=\"{}\" ", rise));
        }

        attrs
    }

    /// Walk WebVTT cue body text character-by-character, translating:
    ///   - `<b>`, `<i>`, `<u>` → Pango bold/italic/underline tags
    ///   - `<c.class>` → `<span …>` with CSS declarations for `.class`
    ///   - `<v …>` (voice) → ignored
    ///   - HTML entities → passed through (Pango understands `&amp;` etc.)
    ///   - Plain text → XML-escaped
    fn process_cue_text(
        &self,
        text: &str,
        markup: &mut String,
        ref_line_height: f32,
        max_font_size: f32,
    ) {
        let mut pos = 0;
        let bytes = text.as_bytes();

        while pos < bytes.len() {
            if bytes[pos] == b'<' {
                // Parse tag
                pos += 1;

                // Check for closing tag
                let is_closing = pos < bytes.len() && bytes[pos] == b'/';
                if is_closing {
                    pos += 1;
                }

                // Read tag name
                let name_start = pos;
                while pos < bytes.len() && bytes[pos] != b'>' && bytes[pos] != b' ' {
                    pos += 1;
                }
                let tag_name = &text[name_start..pos];

                // Skip to end of tag
                while pos < bytes.len() && bytes[pos] != b'>' {
                    pos += 1;
                }
                if pos < bytes.len() {
                    pos += 1; // skip '>'
                }

                if is_closing {
                    // Closing tag
                    let base_tag = tag_name.split('.').next().unwrap_or(tag_name);
                    if base_tag == "c" {
                        markup.push_str("</span>");
                    } else if base_tag == "b" || base_tag == "i" || base_tag == "u" {
                        markup.push_str(&format!("</{}>", base_tag));
                    }
                    // Ignore other closing tags (v, etc.)
                } else {
                    // Opening tag
                    if tag_name.starts_with("c.") {
                        // Class tag: <c.className>
                        let class_data = &tag_name[2..]; // skip "c."
                        let classes: Vec<&str> = class_data.split('.').collect();

                        let mut span_attrs = String::new();
                        let mut applied: HashMap<&str, bool> = HashMap::new();

                        // Process classes in reverse order (last class = highest priority)
                        for class_name in classes.iter().rev() {
                            if class_name.is_empty() {
                                continue;
                            }
                            let declarations = self.get_class_declarations(class_name);
                            for (prop, value) in &declarations {
                                self.apply_class_attr(
                                    prop,
                                    value,
                                    &mut span_attrs,
                                    &mut applied,
                                    ref_line_height,
                                    max_font_size,
                                );
                            }
                        }

                        // Apply default font size if not set by any class
                        if !applied.contains_key("size") {
                            let size = 16.0_f32;
                            span_attrs.push_str(&format!("size=\"{}\" ", (size * 1024.0) as i32));
                        }

                        markup.push_str(&format!("<span {}>", span_attrs));
                    } else if tag_name == "b" || tag_name == "i" || tag_name == "u" {
                        markup.push_str(&format!("<{}>", tag_name));
                    }
                    // Ignore other opening tags (v, etc.)
                }
            } else if bytes[pos] == b'&' {
                // HTML entity
                let entity_start = pos;
                pos += 1;
                while pos < bytes.len() && bytes[pos] != b';' {
                    pos += 1;
                }
                if pos < bytes.len() {
                    pos += 1; // skip ';'
                }
                let entity = &text[entity_start..pos];
                markup.push_str(entity); // pass through (Pango understands XML entities)
            } else {
                // Regular text — escape for Pango markup
                let text_start = pos;
                while pos < bytes.len() && bytes[pos] != b'<' && bytes[pos] != b'&' {
                    pos += 1;
                }
                let text_chunk = &text[text_start..pos];
                markup.push_str(&escape_pango(text_chunk));
            }
        }
    }

    /// Apply a CSS declaration as a Pango span attribute for class-level styling.
    fn apply_class_attr(
        &self,
        prop: &str,
        value: &CssValue,
        attrs: &mut String,
        applied: &mut HashMap<&str, bool>,
        ref_line_height: f32,
        max_font_size: f32,
    ) {
        match prop {
            "color" => {
                if !applied.contains_key("foreground") {
                    if let CssValue::Color(c) = value {
                        if *c != 0 {
                            let (a, r, g, b) = argb_components(*c);
                            attrs.push_str(&format!(
                                "foreground=\"#{:02X}{:02X}{:02X}{:02X}\" ",
                                r, g, b, a
                            ));
                            applied.insert("foreground", true);
                        }
                    }
                }
            }
            "background-color" => {
                if !applied.contains_key("background") {
                    if let CssValue::Color(c) = value {
                        if *c != 0 {
                            let (a, r, g, b) = argb_components(*c);
                            attrs.push_str(&format!(
                                "background=\"#{:02X}{:02X}{:02X}{:02X}\" ",
                                r, g, b, a
                            ));
                            applied.insert("background", true);
                        }
                    }
                }
            }
            "font-size" => {
                if !applied.contains_key("size") {
                    if let CssValue::Length(v, u) = value {
                        if *v > 0.0 {
                            let size = cap_font_size(*v, *u, ref_line_height, max_font_size);
                            attrs.push_str(&format!("size=\"{}\" ", (size * 1024.0) as i32));
                            applied.insert("size", true);
                        }
                    }
                }
            }
            "font-weight" => {
                if !applied.contains_key("weight") {
                    if let CssValue::FontWeight(FontWeight::Bold) = value {
                        attrs.push_str("weight=\"bold\" ");
                        applied.insert("weight", true);
                    }
                }
            }
            "font-family" => {
                if !applied.contains_key("font") {
                    if let CssValue::FontFamily(f) = value {
                        attrs.push_str(&format!("font=\"{}\" ", f));
                        applied.insert("font", true);
                    }
                }
            }
            _ => {}
        }
    }
}

// ---------------------------------------------------------------------------
// CSS Parsing Functions
// ---------------------------------------------------------------------------

/// Strip CSS block comments (`/* … */`).  Nested comments are not supported
/// (CSS spec says they aren't valid anyway).
fn strip_css_comments(css: &str) -> String {
    let mut result = String::with_capacity(css.len());
    let bytes = css.as_bytes();
    let mut i = 0;

    while i < bytes.len() {
        if i + 1 < bytes.len() && bytes[i] == b'/' && bytes[i + 1] == b'*' {
            i += 2;
            while i + 1 < bytes.len() && !(bytes[i] == b'*' && bytes[i + 1] == b'/') {
                i += 1;
            }
            if i + 1 < bytes.len() {
                i += 2; // skip */
            }
        } else {
            result.push(bytes[i] as char);
            i += 1;
        }
    }

    result
}

/// Parse CSS text into a list of rules.
///
/// Splits on `{ … }` pairs, handles one level of brace nesting.
/// Each rule is a (selector, declarations) pair.
fn parse_css_rules(css_text: &str) -> Vec<CssRule> {
    let clean = strip_css_comments(css_text);
    let mut rules = Vec::new();
    let mut pos = 0;

    while pos < clean.len() {
        // Find opening brace
        if let Some(rel_brace) = clean[pos..].find('{') {
            let brace_pos = pos + rel_brace;
            let selector_text = &clean[pos..brace_pos];
            let after_brace = brace_pos + 1;

            // Find matching closing brace (handles nested braces)
            let mut depth = 1;
            let mut close_pos = after_brace;
            while close_pos < clean.len() && depth > 0 {
                match clean.as_bytes()[close_pos] {
                    b'{' => depth += 1,
                    b'}' => depth -= 1,
                    _ => {}
                }
                if depth > 0 {
                    close_pos += 1;
                }
            }

            if depth == 0 {
                let decl_text = &clean[after_brace..close_pos];
                if let Some(selector) = parse_selector(selector_text.trim()) {
                    let declarations = parse_declarations(decl_text);
                    if !declarations.is_empty() {
                        rules.push(CssRule {
                            selector,
                            declarations,
                        });
                    }
                }
                pos = close_pos + 1;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    rules
}

/// Parse a CSS selector string into a `CssSelector`.
///
/// Recognized forms:
///   `::cue` / `cue`           → Universal
///   `::cue(#id)` / `#id`     → Id
///   `::cue(.cls)` / `.cls`   → Class
///   `element`                → Element
fn parse_selector(text: &str) -> Option<CssSelector> {
    let text = text.trim();
    if text.is_empty() {
        return None;
    }

    // Handle ::cue variants
    if text == "::cue" || text == "cue" {
        return Some(CssSelector::Universal);
    }

    // ::cue(#id), ::cue(.class), cue(#id), cue(.class)
    if text.starts_with("::cue(") || text.starts_with("cue(") {
        let inner_start = text.find('(')? + 1;
        let inner_end = text.rfind(')')?;
        let inner = text[inner_start..inner_end].trim();

        if let Some(id) = inner.strip_prefix('#') {
            return Some(CssSelector::Id(id.to_string()));
        } else if let Some(cls) = inner.strip_prefix('.') {
            return Some(CssSelector::Class(cls.to_string()));
        } else {
            return Some(CssSelector::Element(inner.to_string()));
        }
    }

    // Direct selectors
    if let Some(id) = text.strip_prefix('#') {
        return Some(CssSelector::Id(id.to_string()));
    }
    if let Some(cls) = text.strip_prefix('.') {
        return Some(CssSelector::Class(cls.to_string()));
    }

    Some(CssSelector::Element(text.to_string()))
}

/// Parse semicolon-separated CSS declarations from a rule body.
/// Returns (property-name, CssValue) pairs for recognized properties.
fn parse_declarations(text: &str) -> Vec<(String, CssValue)> {
    let mut declarations = Vec::new();

    for decl in text.split(';') {
        let decl = decl.trim();
        if decl.is_empty() {
            continue;
        }

        if let Some((prop, value)) = decl.split_once(':') {
            let prop = prop.trim().to_lowercase();
            let value = value.trim();

            if let Some(css_value) = parse_value(&prop, value) {
                declarations.push((prop, css_value));
            }
        }
    }

    declarations
}

/// Parse a CSS property value into a typed `CssValue`.
/// `!important` is stripped (always wins — we don't track specificity).
fn parse_value(property: &str, value: &str) -> Option<CssValue> {
    let value = value.trim_end_matches("!important").trim();

    match property {
        "color" | "background-color" => parse_color(value).map(CssValue::Color),
        "font-size" => parse_length(value).map(|(v, u)| CssValue::Length(v, u)),
        "font-family" => Some(CssValue::FontFamily(parse_font_family(value))),
        "font-weight" => match value.to_lowercase().as_str() {
            "bold" | "bolder" | "700" | "800" | "900" => {
                Some(CssValue::FontWeight(FontWeight::Bold))
            }
            "normal" | "lighter" | "100" | "200" | "300" | "400" | "500" => {
                Some(CssValue::FontWeight(FontWeight::Normal))
            }
            _ => None,
        },
        "padding" | "padding-top" => parse_length(value).map(|(v, u)| CssValue::Length(v, u)),
        _ => None,
    }
}

// ---------------------------------------------------------------------------
// Color Parsing
// ---------------------------------------------------------------------------

/// Parse a CSS color literal into a packed ARGB `u32`.
///
/// Supports: named colors, `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA`,
/// `rgb(r,g,b)`, `rgba(r,g,b,a)`, and percentage components.
fn parse_color(value: &str) -> Option<u32> {
    let value = value.trim().to_lowercase();

    // Named colors
    let named = match value.as_str() {
        "white" => Some(0xFFFFFFFF_u32),
        "black" => Some(0xFF000000),
        "red" => Some(0xFFFF0000),
        "green" => Some(0xFF008000),
        "lime" => Some(0xFF00FF00),
        "blue" => Some(0xFF0000FF),
        "yellow" => Some(0xFFFFFF00),
        "cyan" | "aqua" => Some(0xFF00FFFF),
        "magenta" | "fuchsia" => Some(0xFFFF00FF),
        "gray" | "grey" => Some(0xFF808080),
        "silver" => Some(0xFFC0C0C0),
        "maroon" => Some(0xFF800000),
        "olive" => Some(0xFF808000),
        "navy" => Some(0xFF000080),
        "purple" => Some(0xFF800080),
        "teal" => Some(0xFF008080),
        "orange" => Some(0xFFFFA500),
        "pink" => Some(0xFFFFC0CB),
        "brown" => Some(0xFFA52A2A),
        "coral" => Some(0xFFFF7F50),
        "gold" => Some(0xFFFFD700),
        "khaki" => Some(0xFFF0E68C),
        "lavender" => Some(0xFFE6E6FA),
        "lightblue" => Some(0xFFADD8E6),
        "lightgreen" => Some(0xFF90EE90),
        "lightyellow" => Some(0xFFFFFFE0),
        "transparent" => Some(0x00000000),
        _ => None,
    };
    if named.is_some() {
        return named;
    }

    // Hex colors
    if let Some(hex) = value.strip_prefix('#') {
        return parse_hex_color(hex);
    }

    // rgb() / rgba()
    if value.starts_with("rgb") {
        return parse_rgb_function(&value);
    }

    None
}

/// Parse hex color (#RGB, #RGBA, #RRGGBB, #RRGGBBAA).
fn parse_hex_color(hex: &str) -> Option<u32> {
    match hex.len() {
        3 => {
            let r = u8::from_str_radix(&hex[0..1], 16).ok()? * 17;
            let g = u8::from_str_radix(&hex[1..2], 16).ok()? * 17;
            let b = u8::from_str_radix(&hex[2..3], 16).ok()? * 17;
            Some(0xFF000000 | (r as u32) << 16 | (g as u32) << 8 | b as u32)
        }
        4 => {
            let r = u8::from_str_radix(&hex[0..1], 16).ok()? * 17;
            let g = u8::from_str_radix(&hex[1..2], 16).ok()? * 17;
            let b = u8::from_str_radix(&hex[2..3], 16).ok()? * 17;
            let a = u8::from_str_radix(&hex[3..4], 16).ok()? * 17;
            Some((a as u32) << 24 | (r as u32) << 16 | (g as u32) << 8 | b as u32)
        }
        6 => {
            let r = u8::from_str_radix(&hex[0..2], 16).ok()?;
            let g = u8::from_str_radix(&hex[2..4], 16).ok()?;
            let b = u8::from_str_radix(&hex[4..6], 16).ok()?;
            Some(0xFF000000 | (r as u32) << 16 | (g as u32) << 8 | b as u32)
        }
        8 => {
            let r = u8::from_str_radix(&hex[0..2], 16).ok()?;
            let g = u8::from_str_radix(&hex[2..4], 16).ok()?;
            let b = u8::from_str_radix(&hex[4..6], 16).ok()?;
            let a = u8::from_str_radix(&hex[6..8], 16).ok()?;
            Some((a as u32) << 24 | (r as u32) << 16 | (g as u32) << 8 | b as u32)
        }
        _ => None,
    }
}

/// Parse rgb() or rgba() function.
fn parse_rgb_function(value: &str) -> Option<u32> {
    let start = value.find('(')?;
    let end = value.rfind(')')?;
    let inner = &value[start + 1..end];
    let parts: Vec<&str> = inner.split(',').map(|s| s.trim()).collect();

    match parts.len() {
        3 => {
            let r = parse_color_component(parts[0])?;
            let g = parse_color_component(parts[1])?;
            let b = parse_color_component(parts[2])?;
            Some(0xFF000000 | (r as u32) << 16 | (g as u32) << 8 | b as u32)
        }
        4 => {
            let r = parse_color_component(parts[0])?;
            let g = parse_color_component(parts[1])?;
            let b = parse_color_component(parts[2])?;
            let a = parse_alpha_component(parts[3])?;
            Some((a as u32) << 24 | (r as u32) << 16 | (g as u32) << 8 | b as u32)
        }
        _ => None,
    }
}

/// Parse a color component (0-255 or 0%-100%).
fn parse_color_component(s: &str) -> Option<u8> {
    let s = s.trim();
    if let Some(pct) = s.strip_suffix('%') {
        let v: f32 = pct.trim().parse().ok()?;
        Some((v.clamp(0.0, 100.0) * 2.55) as u8)
    } else {
        let v: i32 = s.parse().ok()?;
        Some(v.clamp(0, 255) as u8)
    }
}

/// Parse an alpha component (0.0-1.0 or 0%-100%).
fn parse_alpha_component(s: &str) -> Option<u8> {
    let s = s.trim();
    if let Some(pct) = s.strip_suffix('%') {
        let v: f32 = pct.trim().parse().ok()?;
        Some((v.clamp(0.0, 100.0) * 2.55) as u8)
    } else {
        let v: f32 = s.parse().ok()?;
        Some((v.clamp(0.0, 1.0) * 255.0) as u8)
    }
}

// ---------------------------------------------------------------------------
// Length / Unit Parsing
// ---------------------------------------------------------------------------

/// Parse a CSS length value (e.g., "16px", "100%", "1.5em").
fn parse_length(value: &str) -> Option<(f32, CssUnit)> {
    let value = value.trim();

    if let Some(v) = value.strip_suffix("px") {
        let num: f32 = v.trim().parse().ok()?;
        Some((num, CssUnit::Px))
    } else if let Some(v) = value.strip_suffix('%') {
        let num: f32 = v.trim().parse().ok()?;
        Some((num, CssUnit::Pct))
    } else if let Some(v) = value.strip_suffix("em") {
        let num: f32 = v.trim().parse().ok()?;
        Some((num, CssUnit::Em))
    } else {
        // Try as bare number (assumed px)
        let num: f32 = value.parse().ok()?;
        Some((num, CssUnit::Px))
    }
}

// ---------------------------------------------------------------------------
// Font Family Parsing
// ---------------------------------------------------------------------------

/// Parse a CSS font-family value.
fn parse_font_family(value: &str) -> String {
    let value = value.trim();

    // Take the first font family (before comma)
    let first = value.split(',').next().unwrap_or(value).trim();

    // Strip quotes
    let first = first
        .trim_start_matches('"')
        .trim_end_matches('"')
        .trim_start_matches('\'')
        .trim_end_matches('\'');

    first.to_string()
}

// ---------------------------------------------------------------------------
// Utility Functions
// ---------------------------------------------------------------------------

/// Extract ARGB components from a u32 color.
fn argb_components(color: u32) -> (u8, u8, u8, u8) {
    let a = ((color >> 24) & 0xFF) as u8;
    let r = ((color >> 16) & 0xFF) as u8;
    let g = ((color >> 8) & 0xFF) as u8;
    let b = (color & 0xFF) as u8;
    (a, r, g, b)
}

/// Clamp font size to `max_font_size`.  Percentage and em values are first
/// resolved relative to `ref_line_height` or a 16 px base respectively.
fn cap_font_size(size: f32, unit: CssUnit, ref_line_height: f32, max_font_size: f32) -> f32 {
    match unit {
        CssUnit::Px => size.min(max_font_size),
        CssUnit::Pct => ((size / 100.0) * ref_line_height).min(max_font_size),
        CssUnit::Em => (size * 16.0).min(max_font_size),
    }
}

/// Escape text for Pango markup (XML special characters).
fn escape_pango(text: &str) -> String {
    let mut result = String::with_capacity(text.len());
    for c in text.chars() {
        match c {
            '&' => result.push_str("&amp;"),
            '<' => result.push_str("&lt;"),
            '>' => result.push_str("&gt;"),
            '"' => result.push_str("&quot;"),
            '\'' => result.push_str("&apos;"),
            _ => result.push(c),
        }
    }
    result
}

// ---------------------------------------------------------------------------
// C FFI Functions
// ---------------------------------------------------------------------------

/// Create a new GstCssParse instance.
#[no_mangle]
pub extern "C" fn gst_cssparse_new() -> *mut GstCssParse {
    Box::into_raw(Box::new(GstCssParse::new()))
}

/// Free a GstCssParse instance.
#[no_mangle]
pub extern "C" fn gst_cssparse_free(ptr: *mut GstCssParse) {
    if !ptr.is_null() {
        unsafe {
            drop(Box::from_raw(ptr));
        }
    }
}

/// Reinitialize computed properties (keeps accumulated CSS rules).
#[no_mangle]
pub extern "C" fn gst_cssparse_reinit(ptr: *mut GstCssParse) {
    if let Some(p) = unsafe { ptr.as_mut() } {
        p.reinit();
    }
}

/// Set the current cue ID for CSS selector matching.
#[no_mangle]
pub extern "C" fn gst_cssparse_set_cue_id(ptr: *mut GstCssParse, cue_id: *const c_char) {
    if let Some(p) = unsafe { ptr.as_mut() } {
        if cue_id.is_null() {
            p.current_cue_id = None;
        } else {
            let s = unsafe { CStr::from_ptr(cue_id) };
            p.current_cue_id = s.to_str().ok().map(|s| s.to_string());
        }
    }
}

/// Parse CSS data and apply matching rules. Returns 0 on success.
#[no_mangle]
pub extern "C" fn gst_cssparse_parse(ptr: *mut GstCssParse, css_data: *const c_char) -> c_int {
    if let Some(p) = unsafe { ptr.as_mut() } {
        let data = if css_data.is_null() {
            ""
        } else {
            match unsafe { CStr::from_ptr(css_data) }.to_str() {
                Ok(s) => s,
                Err(_) => return 1,
            }
        };
        p.parse(data)
    } else {
        1 // error
    }
}

/// Generate Pango markup from parsed CSS and cue text.
/// Returns a g_malloc'd string that must be freed with g_free().
#[no_mangle]
pub extern "C" fn gst_cssparse_to_pango_markup(
    ptr: *mut GstCssParse,
    text: *const c_char,
) -> *mut c_char {
    if ptr.is_null() || text.is_null() {
        return std::ptr::null_mut();
    }

    let p = unsafe { &*ptr };
    let text = match unsafe { CStr::from_ptr(text) }.to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };

    let markup = p.to_pango_markup(text);
    string_to_c(&markup)
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_hex_color() {
        assert_eq!(parse_hex_color("fff"), Some(0xFFFFFFFF));
        assert_eq!(parse_hex_color("000"), Some(0xFF000000));
        assert_eq!(parse_hex_color("ff0000"), Some(0xFFFF0000));
        assert_eq!(parse_hex_color("00ff0080"), Some(0x8000FF00));
    }

    #[test]
    fn test_parse_named_color() {
        assert_eq!(parse_color("white"), Some(0xFFFFFFFF));
        assert_eq!(parse_color("black"), Some(0xFF000000));
        assert_eq!(parse_color("red"), Some(0xFFFF0000));
        assert_eq!(parse_color("transparent"), Some(0x00000000));
    }

    #[test]
    fn test_parse_rgb() {
        assert_eq!(parse_color("rgb(255, 0, 0)"), Some(0xFFFF0000));
        assert_eq!(parse_color("rgba(0, 255, 0, 0.5)"), Some(0x7F00FF00));
    }

    #[test]
    fn test_parse_length() {
        assert_eq!(parse_length("16px"), Some((16.0, CssUnit::Px)));
        assert_eq!(parse_length("100%"), Some((100.0, CssUnit::Pct)));
        assert_eq!(parse_length("1.5em"), Some((1.5, CssUnit::Em)));
    }

    #[test]
    fn test_parse_selector() {
        assert_eq!(parse_selector("::cue"), Some(CssSelector::Universal));
        assert_eq!(
            parse_selector("::cue(#intro)"),
            Some(CssSelector::Id("intro".to_string()))
        );
        assert_eq!(
            parse_selector("::cue(.highlight)"),
            Some(CssSelector::Class("highlight".to_string()))
        );
        assert_eq!(
            parse_selector(".myclass"),
            Some(CssSelector::Class("myclass".to_string()))
        );
    }

    #[test]
    fn test_parse_css_rules() {
        let css = r#"
            ::cue { color: white; font-size: 16px; }
            ::cue(#intro) { color: red; }
            ::cue(.highlight) { background-color: yellow; }
        "#;
        let rules = parse_css_rules(css);
        assert_eq!(rules.len(), 3);
        assert_eq!(rules[0].selector, CssSelector::Universal);
        assert_eq!(rules[1].selector, CssSelector::Id("intro".to_string()));
        assert_eq!(
            rules[2].selector,
            CssSelector::Class("highlight".to_string())
        );
    }

    #[test]
    fn test_strip_comments() {
        assert_eq!(strip_css_comments("a /* comment */ b"), "a  b");
        assert_eq!(strip_css_comments("no comments"), "no comments");
    }

    #[test]
    fn test_escape_pango() {
        assert_eq!(escape_pango("a<b>c"), "a&lt;b&gt;c");
        assert_eq!(escape_pango("a&b"), "a&amp;b");
    }

    #[test]
    fn test_parse_font_family() {
        assert_eq!(parse_font_family("\"Arial\""), "Arial");
        assert_eq!(parse_font_family("Arial, sans-serif"), "Arial");
        assert_eq!(parse_font_family("sans-serif"), "sans-serif");
    }

    #[test]
    fn test_full_parse_and_match() {
        let mut p = GstCssParse::new();
        p.current_cue_id = Some("intro".to_string());
        p.parse("::cue { color: white; } ::cue(#intro) { color: red; }");
        // The #intro rule should override ::cue
        assert_eq!(p.color, 0xFFFF0000); // red
    }

    #[test]
    fn test_class_matching() {
        let mut p = GstCssParse::new();
        p.parse("::cue(.highlight) { color: yellow; }");
        let decls = p.get_class_declarations("highlight");
        assert_eq!(decls.len(), 1);
        assert_eq!(decls[0].0, "color");
    }
}
