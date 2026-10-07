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
        view.setValue(false, forKey: "drawsBackground")
        if view.responds(to: Selector(("setAutomaticallyAdjustsContentInsets:"))) {
            view.setValue(false, forKey: "automaticallyAdjustsContentInsets")
        }
        WindowChrome.suppressScrollPockets(view, hidden: true)
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
        WindowChrome.suppressScrollPockets(view, hidden: true)
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
            WindowChrome.suppressScrollPockets(webView, hidden: true)
            guard pendingFollow != scrolledFollow else { return }
            scrolledFollow = pendingFollow
            MarkdownWebView.scrollToEnd(webView)
        }
    }
}

struct WindowChrome: NSViewRepresentable {
    var title: String
    var hideTitleBar: Bool
    var backgroundHex: String
    var dark: Bool

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    func makeNSView(context: Context) -> NSView {
        NSView()
    }

    func updateNSView(_ view: NSView, context: Context) {
        context.coordinator.title = title
        context.coordinator.hideTitleBar = hideTitleBar
        context.coordinator.background = Self.nsColor(hex: backgroundHex)
        context.coordinator.dark = dark
        DispatchQueue.main.async {
            context.coordinator.attach(view.window)
        }
    }

    static func nsColor(hex: String) -> NSColor {
        var s = hex.trimmingCharacters(in: .whitespacesAndNewlines)
        if s.hasPrefix("#") { s.removeFirst() }
        var n: UInt64 = 0
        Scanner(string: s).scanHexInt64(&n)
        return NSColor(
            srgbRed: CGFloat((n >> 16) & 0xff) / 255,
            green: CGFloat((n >> 8) & 0xff) / 255,
            blue: CGFloat(n & 0xff) / 255,
            alpha: 1
        )
    }

    final class Coordinator {
        var title = "omamd"
        var hideTitleBar = false
        var background = NSColor.black
        var dark = true
        private weak var window: NSWindow?
        private var observers: [NSObjectProtocol] = []

        func attach(_ window: NSWindow?) {
            guard let window else { return }
            if self.window !== window {
                detach()
                self.window = window
                let nc = NotificationCenter.default
                let names: [Notification.Name] = [
                    NSWindow.didBecomeKeyNotification,
                    NSWindow.didResignKeyNotification,
                    NSWindow.didBecomeMainNotification,
                    NSWindow.didResignMainNotification,
                ]
                observers = names.map { name in
                    nc.addObserver(forName: name, object: window, queue: .main) { [weak self] _ in
                        self?.apply()
                    }
                }
            }
            apply()
        }

        func detach() {
            observers.forEach { NotificationCenter.default.removeObserver($0) }
            observers.removeAll()
            window = nil
        }

        deinit { detach() }

        func apply() {
            guard let window else { return }
            window.title = title
            window.titlebarSeparatorStyle = .none
            window.backgroundColor = background
            window.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
            /* Keep .titled so the window stays AXStandardWindow. Yabai
             * floats AXDialog, which is what you get without .titled. */
            window.styleMask.insert(.titled)
            if hideTitleBar {
                window.titleVisibility = .hidden
                window.titlebarAppearsTransparent = true
                window.styleMask.insert(.fullSizeContentView)
                window.isMovableByWindowBackground = true
            } else {
                window.titleVisibility = .visible
                window.titlebarAppearsTransparent = false
                window.styleMask.remove(.fullSizeContentView)
                window.isMovableByWindowBackground = false
            }
            hideTitlebarMaterial(window, hidden: hideTitleBar)
            setTrafficLights(window, hidden: hideTitleBar)
            if let root = window.contentView {
                WindowChrome.suppressScrollPockets(root, hidden: hideTitleBar)
            }
        }

        func setTrafficLights(_ window: NSWindow, hidden: Bool) {
            let buttons: [NSWindow.ButtonType] = [.closeButton, .miniaturizeButton, .zoomButton]
            for type in buttons {
                let button = window.standardWindowButton(type)
                button?.isHidden = hidden
                button?.alphaValue = hidden ? 0 : 1
                button?.superview?.isHidden = hidden
                button?.superview?.alphaValue = hidden ? 0 : 1
            }
            let bar = window.standardWindowButton(.closeButton)?.superview
            bar?.superview?.isHidden = hidden
            bar?.superview?.alphaValue = hidden ? 0 : 1
        }

        func hideTitlebarMaterial(_ window: NSWindow, hidden: Bool) {
            guard let frame = window.contentView?.superview else { return }
            func walk(_ view: NSView) {
                let typeName = String(describing: type(of: view))
                if view is NSVisualEffectView
                    || typeName.contains("Titlebar")
                    || typeName.contains("ThemeWidget")
                    || typeName.contains("ThemeClose")
                    || typeName.contains("ThemeZoom") {
                    view.isHidden = hidden
                    view.alphaValue = hidden ? 0 : 1
                }
                if view.frame.height > 0 && view.frame.height <= 2 && view.frame.width > 40 {
                    view.isHidden = hidden
                }
                view.subviews.forEach(walk)
            }
            for sub in frame.subviews where sub !== window.contentView {
                walk(sub)
            }
        }
    }

    /* macOS 26 scroll pockets paint a 32pt material strip in hidden
     * title bars (same bug Ghostty worked around). */
    static func suppressScrollPockets(_ root: NSView, hidden: Bool) {
        func walk(_ view: NSView) {
            let name = String(describing: type(of: view))
            if name.contains("ScrollPocket") {
                view.isHidden = hidden
                view.alphaValue = hidden ? 0 : 1
            }
            if name.contains("BackdropView") && view.frame.height > 0 && view.frame.height <= 40 {
                view.isHidden = hidden
                view.alphaValue = hidden ? 0 : 1
            }
            if hidden {
                if view.responds(to: Selector(("setAllowedPocketEdges:"))) {
                    view.setValue(0, forKey: "allowedPocketEdges")
                }
                if view.responds(to: Selector(("setAlwaysShownPocketEdges:"))) {
                    view.setValue(0, forKey: "alwaysShownPocketEdges")
                }
            }
            view.subviews.forEach(walk)
        }
        walk(root)
    }
}
