import CoreText
import Foundation

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
}
