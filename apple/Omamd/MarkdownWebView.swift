import AppKit
import SwiftUI
import WebKit

struct MarkdownWebView: NSViewRepresentable {
    var html: String
    var baseURL: URL?
    var docDir: URL?
    var followGeneration: UInt
    var onOpenMarkdown: (URL) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    func makeNSView(context: Context) -> WKWebView {
        let view = WKWebView(frame: .zero, configuration: WKWebViewConfiguration())
        view.navigationDelegate = context.coordinator
        return view
    }

    func updateNSView(_ view: WKWebView, context: Context) {
        context.coordinator.docDir = docDir
        context.coordinator.onOpenMarkdown = onOpenMarkdown
        if context.coordinator.html != html {
            context.coordinator.html = html
            context.coordinator.pendingFollow = followGeneration
            view.loadHTMLString(html, baseURL: baseURL)
        } else if context.coordinator.scrolledFollow != followGeneration {
            context.coordinator.scrolledFollow = followGeneration
            Self.scrollToEnd(view)
        }
    }

    static func scrollToEnd(_ view: WKWebView) {
        view.evaluateJavaScript(
            """
            (function(){
              var r=document.scrollingElement||document.documentElement;
              var t=Math.max(0,r.scrollHeight-r.clientHeight);
              try{r.scrollTo({top:t,behavior:'smooth'});}
              catch(e){r.scrollTop=t;}
            })();
            """,
            completionHandler: nil
        )
    }

    final class Coordinator: NSObject, WKNavigationDelegate {
        var html = ""
        var docDir: URL?
        var onOpenMarkdown: (URL) -> Void = { _ in }
        var pendingFollow: UInt = 0
        var scrolledFollow: UInt = 0

        func webView(
            _ webView: WKWebView,
            decidePolicyFor action: WKNavigationAction,
            decisionHandler: @escaping (WKNavigationActionPolicy) -> Void
        ) {
            guard action.navigationType == .linkActivated else {
                decisionHandler(.allow)
                return
            }
            guard let url = action.request.url else {
                decisionHandler(.cancel)
                return
            }
            if let scheme = url.scheme, ["http", "https", "mailto"].contains(scheme) {
                NSWorkspace.shared.open(url)
                decisionHandler(.cancel)
                return
            }
            if url.isFileURL, let docDir, Omamd.isMarkdown(url: url) {
                let path = url.path
                let dir = docDir.path
                let realPath = URL(fileURLWithPath: path).resolvingSymlinksInPath().path
                let realDir = URL(fileURLWithPath: dir).resolvingSymlinksInPath().path
                if realPath == realDir || realPath.hasPrefix(realDir.hasSuffix("/") ? realDir : realDir + "/") {
                    onOpenMarkdown(url)
                }
            }
            decisionHandler(.cancel)
        }

        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
            guard pendingFollow != scrolledFollow else { return }
            scrolledFollow = pendingFollow
            MarkdownWebView.scrollToEnd(webView)
        }
    }
}

struct WindowTitleSetter: NSViewRepresentable {
    var title: String

    func makeNSView(context: Context) -> NSView {
        NSView()
    }

    func updateNSView(_ view: NSView, context: Context) {
        view.window?.title = title
    }
}
