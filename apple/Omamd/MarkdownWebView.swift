import SwiftUI
import WebKit
#if os(macOS)
import AppKit
#else
import UIKit
#endif

struct MarkdownWebView {
    var html: String
    var baseURL: URL?
    var docDir: URL?
    var followGeneration: UInt
    var hideTitleBar: Bool
    var onOpenMarkdown: (URL) -> Void

    func makeCoordinator() -> MarkdownWebCoordinator {
        MarkdownWebCoordinator()
    }

    static func load(_ html: String, into view: WKWebView, baseURL: URL?) {
        #if os(iOS)
        /* loadHTMLString + file:// @font-face stays blank on iOS.
         * Write the page and load it as a file; Core Text already
         * registered the bundled faces. */
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("omamd-page", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let file = dir.appendingPathComponent("index.html")
        do {
            try html.write(to: file, atomically: true, encoding: .utf8)
            view.loadFileURL(file, allowingReadAccessTo: dir)
        } catch {
            view.loadHTMLString(html, baseURL: baseURL)
        }
        #else
        view.loadHTMLString(html, baseURL: baseURL)
        #endif
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

    fileprivate func bind(_ view: WKWebView, context: BoundContext) {
        let coordinator = context.coordinator
        coordinator.docDir = docDir
        coordinator.onOpenMarkdown = onOpenMarkdown
        coordinator.hideTitleBar = hideTitleBar
        coordinator.applyChrome(view)
        if coordinator.html != html {
            coordinator.html = html
            coordinator.pendingFollow = followGeneration
            Self.load(html, into: view, baseURL: baseURL)
        } else if coordinator.scrolledFollow != followGeneration {
            coordinator.scrolledFollow = followGeneration
            Self.scrollToEnd(view)
        }
    }

    fileprivate struct BoundContext {
        var coordinator: MarkdownWebCoordinator
    }
}

#if os(macOS)
extension MarkdownWebView: NSViewRepresentable {
    func makeNSView(context: Context) -> WKWebView {
        let view = WKWebView(frame: .zero, configuration: WKWebViewConfiguration())
        view.navigationDelegate = context.coordinator
        view.setValue(false, forKey: "drawsBackground")
        context.coordinator.applyChrome(view)
        return view
    }

    func updateNSView(_ view: WKWebView, context: Context) {
        bind(view, context: BoundContext(coordinator: context.coordinator))
    }
}
#else
final class MarkdownWebContainer: UIView {
    let webView: WKWebView

    init(webView: WKWebView) {
        self.webView = webView
        super.init(frame: .zero)
        backgroundColor = .clear
        addSubview(webView)
        webView.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            webView.topAnchor.constraint(equalTo: topAnchor),
            webView.bottomAnchor.constraint(equalTo: bottomAnchor),
            webView.leadingAnchor.constraint(equalTo: leadingAnchor),
            webView.trailingAnchor.constraint(equalTo: trailingAnchor),
        ])
    }

    required init?(coder: NSCoder) { nil }
}

extension MarkdownWebView: UIViewRepresentable {
    func makeUIView(context: Context) -> MarkdownWebContainer {
        let view = WKWebView(frame: .zero, configuration: WKWebViewConfiguration())
        view.navigationDelegate = context.coordinator
        view.isOpaque = false
        view.backgroundColor = .clear
        view.scrollView.backgroundColor = .clear
        view.scrollView.contentInsetAdjustmentBehavior = .never
        view.scrollView.alwaysBounceVertical = true
        if #available(iOS 16.4, *) {
            view.isInspectable = true
        }
        context.coordinator.applyChrome(view)
        return MarkdownWebContainer(webView: view)
    }

    func updateUIView(_ container: MarkdownWebContainer, context: Context) {
        bind(container.webView, context: BoundContext(coordinator: context.coordinator))
    }
}
#endif

final class MarkdownWebCoordinator: NSObject, WKNavigationDelegate {
    var html = ""
    var docDir: URL?
    var onOpenMarkdown: (URL) -> Void = { _ in }
    var pendingFollow: UInt = 0
    var scrolledFollow: UInt = 0
    var hideTitleBar = false

    func applyChrome(_ webView: WKWebView) {
        #if os(macOS)
        WindowChrome.applyPageInsets(webView, flushTop: hideTitleBar)
        WindowChrome.suppressScrollPockets(webView, hidden: hideTitleBar)
        #else
        webView.scrollView.contentInsetAdjustmentBehavior = .never
        if #available(iOS 26.0, *) {
            webView.obscuredContentInsets = .zero
        }
        #endif
    }

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
            #if os(macOS)
            NSWorkspace.shared.open(url)
            #else
            UIApplication.shared.open(url)
            #endif
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
        applyChrome(webView)
        guard pendingFollow != scrolledFollow else { return }
        scrolledFollow = pendingFollow
        MarkdownWebView.scrollToEnd(webView)
    }

    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        NSLog("omamd webview fail: \(error.localizedDescription)")
    }

    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        NSLog("omamd webview provisional fail: \(error.localizedDescription)")
    }
}

#if os(macOS)
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
                WindowChrome.flushWebInsets(root, flushTop: hideTitleBar)
            }
            /* WebKit reapplies title-bar insets after the style mask
             * change; flush once more on the next turn. */
            DispatchQueue.main.async { [weak self] in
                guard let self, let root = self.window?.contentView else { return }
                WindowChrome.suppressScrollPockets(root, hidden: self.hideTitleBar)
                WindowChrome.flushWebInsets(root, flushTop: self.hideTitleBar)
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

    /* A titled window still reports a ~28–32pt title-bar overlay.
     * Zero WKWebView's layout insets so the page sits under the
     * window curve instead of leaving an empty strip. */
    static func applyPageInsets(_ webView: WKWebView, flushTop: Bool) {
        if #available(macOS 26.0, *) {
            webView.obscuredContentInsets = NSEdgeInsets()
        }
        if webView.responds(to: Selector(("setAutomaticallyAdjustsContentInsets:"))) {
            webView.setValue(false, forKey: "automaticallyAdjustsContentInsets")
        }
        func walk(_ view: NSView) {
            if let scroll = view as? NSScrollView {
                scroll.automaticallyAdjustsContentInsets = false
                if flushTop {
                    scroll.contentInsets = NSEdgeInsets()
                    scroll.scrollerInsets = NSEdgeInsets()
                    scroll.contentView.automaticallyAdjustsContentInsets = false
                    scroll.contentView.contentInsets = NSEdgeInsets()
                }
            }
            view.subviews.forEach(walk)
        }
        walk(webView)
        if flushTop {
            let titlebar = webView.window.map { window -> CGFloat in
                guard let content = window.contentView else { return 0 }
                return max(0, content.bounds.height - window.contentLayoutRect.height)
            } ?? 0
            webView.additionalSafeAreaInsets = NSEdgeInsets(
                top: -titlebar,
                left: 0,
                bottom: 0,
                right: 0
            )
        } else {
            webView.additionalSafeAreaInsets = NSEdgeInsets()
        }
    }

    static func flushWebInsets(_ root: NSView, flushTop: Bool) {
        func walk(_ view: NSView) {
            if let web = view as? WKWebView {
                applyPageInsets(web, flushTop: flushTop)
            }
            view.subviews.forEach(walk)
        }
        walk(root)
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
#endif
