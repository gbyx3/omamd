import SwiftUI

struct ContentView: View {
    private let html: String
    private let baseURL: URL?

    init() {
        let fontDir = BundledFonts.directory
        let markdown: String
        if let url = Bundle.main.url(forResource: "welcome", withExtension: "md"),
           let text = try? String(contentsOf: url, encoding: .utf8) {
            markdown = text
        } else {
            markdown = "# omamd\n\nMissing welcome.md in the app bundle.\n"
        }
        baseURL = fontDir
        html = Omamd.page(
            markdown: markdown,
            title: "welcome.md",
            fontDir: fontDir
        )
    }

    var body: some View {
        MarkdownWebView(html: html, baseURL: baseURL)
            .frame(minWidth: 520, minHeight: 640)
    }
}
