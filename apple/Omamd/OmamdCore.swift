import CoreText
import Foundation
import SwiftUI

enum BundledFonts {
    static var directory: URL? {
        Bundle.main.resourceURL?.appendingPathComponent("fonts")
    }

    static func register() {
        guard let directory else { return }
        let names = [
            "iAWriterMonoS-Regular.ttf",
            "iAWriterMonoS-Italic.ttf",
            "iAWriterMonoS-Bold.ttf",
            "iAWriterMonoS-BoldItalic.ttf",
        ]
        for name in names {
            CTFontManagerRegisterFontsForURL(
                directory.appendingPathComponent(name) as CFURL,
                .process,
                nil
            )
        }
    }
}

enum Omamd {
    static func page(
        markdown: String,
        title: String,
        themePath: String? = nil,
        fontDir: URL? = nil
    ) -> String {
        markdown.withCString { mdPtr in
            guard let fragment = markdown_to_html(mdPtr, markdown.utf8.count) else {
                return ""
            }
            defer { free(fragment) }

            var palette = Palette()
            if let themePath {
                palette_default(&palette)
                _ = themePath.withCString { palette_load_file(&palette, $0) }
            } else {
                palette_load(&palette, nil)
            }

            let css = omamd_css(&palette)
            defer { css.map { free($0) } }

            return title.withCString { titlePtr in
                let document: UnsafeMutablePointer<CChar>?
                if let fontDir {
                    document = fontDir.path.withCString { fontPtr in
                        omamd_document(titlePtr, css, fragment, fontPtr)
                    }
                } else {
                    document = omamd_document(titlePtr, css, fragment, nil)
                }
                defer { document.map { free($0) } }
                return document.map { String(cString: $0) } ?? ""
            }
        }
    }

    static func palette(themePath: String? = nil) -> OmamdPalette {
        var p = Palette()
        if let themePath {
            palette_default(&p)
            _ = themePath.withCString { palette_load_file(&p, $0) }
        } else {
            palette_load(&p, nil)
        }
        return OmamdPalette(
            bg: cField(&p.bg),
            fg: cField(&p.fg),
            muted: cField(&p.muted),
            accent: cField(&p.accent),
            codeBg: cField(&p.code_bg),
            surface: cField(&p.surface),
            sel: cField(&p.sel),
            dark: p.dark != 0
        )
    }

    static func isMarkdown(url: URL) -> Bool {
        url.path.withCString { omamd_is_markdown_path($0) != 0 }
    }

    static var userThemePath: String {
        var buf = [CChar](repeating: 0, count: 4096)
        theme_user_config_path(&buf, buf.count)
        return String(cString: buf)
    }
}

struct OmamdPalette: Equatable {
    var bg: String
    var fg: String
    var muted: String
    var accent: String
    var codeBg: String
    var surface: String
    var sel: String
    var dark: Bool
}

extension Color {
    init(hex: String) {
        var s = hex.trimmingCharacters(in: .whitespacesAndNewlines)
        if s.hasPrefix("#") {
            s.removeFirst()
        }
        var n: UInt64 = 0
        Scanner(string: s).scanHexInt64(&n)
        self.init(
            .sRGB,
            red: Double((n >> 16) & 0xff) / 255,
            green: Double((n >> 8) & 0xff) / 255,
            blue: Double(n & 0xff) / 255,
            opacity: 1
        )
    }
}

private func cField<T>(_ value: inout T) -> String {
    withUnsafeBytes(of: &value) { raw in
        guard let base = raw.baseAddress else { return "" }
        return String(cString: base.assumingMemoryBound(to: CChar.self))
    }
}
