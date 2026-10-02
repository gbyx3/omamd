import SwiftUI
import WebKit

struct MarkdownWebView: NSViewRepresentable {
    var html: String
    var baseURL: URL?

    func makeNSView(context: Context) -> WKWebView {
        WKWebView(frame: .zero, configuration: WKWebViewConfiguration())
    }

    func updateNSView(_ view: WKWebView, context: Context) {
        view.loadHTMLString(html, baseURL: baseURL)
    }
}
