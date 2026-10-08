import SwiftUI
import UniformTypeIdentifiers
#if os(macOS)
import AppKit
#endif

struct ContentView: View {
    @EnvironmentObject private var viewer: Viewer

    var body: some View {
        ZStack {
            source
                .opacity(viewer.mode == .source ? 1 : 0)
                .allowsHitTesting(viewer.mode == .source)
                #if os(iOS)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                #endif
                #if os(macOS)
                .ignoresSafeArea(.container, edges: viewer.hideTitleBar ? .top : [])
                #endif
            preview
                .opacity(viewer.mode == .preview ? 1 : 0)
                .allowsHitTesting(viewer.mode == .preview)
                #if os(iOS)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                #endif
                #if os(macOS)
                .ignoresSafeArea(.container, edges: viewer.hideTitleBar ? .top : [])
                #endif

            VStack {
                HStack {
                    Spacer()
                    OverlayIconButton(
                        systemName: viewer.mode == .preview
                            ? "chevron.left.forwardslash.chevron.right"
                            : "eye",
                        help: viewer.previewHelp,
                        action: viewer.toggleMode
                    )
                }
                Spacer()
                HStack(spacing: 8) {
                    Spacer()
                    OverlayIconButton(
                        systemName: "folder",
                        help: "Open",
                        action: viewer.openPanel
                    )
                    themeButton
                    OverlayIconButton(
                        systemName: viewer.followEnabled ? "pin.fill" : "pin.slash",
                        help: viewer.followEnabled
                            ? "Follow on — jump to the end when the file changes"
                            : "Follow off — keep your scroll when the file changes",
                        action: viewer.toggleFollow
                    )
                }
            }
            .padding(14)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background {
            Color(hex: viewer.palette.bg)
                #if os(iOS)
                .ignoresSafeArea()
                #endif
        }
        .preferredColorScheme(viewer.palette.dark ? .dark : .light)
        #if os(macOS)
        .background(WindowChrome(
            title: viewer.windowTitle,
            hideTitleBar: viewer.hideTitleBar,
            backgroundHex: viewer.palette.bg,
            dark: viewer.palette.dark
        ))
        .frame(minWidth: 520, minHeight: 640)
        .ignoresSafeArea(.container, edges: viewer.hideTitleBar ? .top : [])
        .onDrop(of: [.fileURL], isTargeted: nil, perform: viewer.drop)
        #endif
        #if os(iOS)
        .fileImporter(
            isPresented: importerPresented,
            allowedContentTypes: viewer.importer == .theme
                ? viewer.themeTypes
                : viewer.markdownTypes,
            allowsMultipleSelection: false
        ) { result in
            let kind = viewer.importer
            viewer.importer = nil
            guard case .success(let urls) = result, let url = urls.first else { return }
            if kind == .theme {
                viewer.importTheme(url)
            } else {
                viewer.loadImported(url)
            }
        }
        .alert("omamd", isPresented: errorPresented) {
            Button("OK", role: .cancel) { viewer.loadError = nil }
        } message: {
            Text(viewer.loadError ?? "")
        }
        #endif
    }

    @ViewBuilder
    private var themeButton: some View {
        #if os(macOS)
        ThemeDropUpButton()
            .frame(width: 34, height: 34)
            .help("Theme")
            .accessibilityLabel("Theme")
        #else
        Menu {
            Picker("Theme", selection: themeBinding) {
                Text("Default").tag(ThemeCatalog.defaultID)
                ForEach(ThemeCatalog.bundled) { theme in
                    Text(theme.name).tag(theme.id)
                }
                if viewer.themeID == ThemeCatalog.customID {
                    Text("Custom").tag(ThemeCatalog.customID)
                }
            }
            Divider()
            Button("Choose File…") { viewer.chooseThemeFile() }
        } label: {
            OverlayGlyph(systemName: "paintpalette")
        }
        .menuIndicator(.hidden)
        .help("Theme")
        .accessibilityLabel("Theme")
        #endif
    }

    #if os(iOS)
    private var themeBinding: Binding<String> {
        Binding(
            get: { viewer.themeID },
            set: { viewer.selectTheme($0) }
        )
    }

    private var importerPresented: Binding<Bool> {
        Binding(
            get: { viewer.importer != nil },
            set: { if !$0 { viewer.importer = nil } }
        )
    }

    private var errorPresented: Binding<Bool> {
        Binding(
            get: { viewer.loadError != nil },
            set: { if !$0 { viewer.loadError = nil } }
        )
    }
    #endif

    private var preview: some View {
        MarkdownWebView(
            html: viewer.html,
            baseURL: viewer.docDir,
            docDir: viewer.docDir,
            followGeneration: UInt(viewer.followGeneration),
            hideTitleBar: viewer.hideTitleBar,
            onOpenMarkdown: viewer.openMarkdownLink
        )
    }

    private var source: some View {
        ScrollViewReader { proxy in
            ScrollView {
                Text(viewer.markdown)
                    .font(.custom("iA Writer Mono S", size: 15))
                    .foregroundStyle(Color(hex: viewer.palette.fg))
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(18)
                Color.clear.frame(height: 1).id("end")
            }
            .background(Color(hex: viewer.palette.bg))
            .onChange(of: viewer.followGeneration) { _, _ in
                withAnimation {
                    proxy.scrollTo("end", anchor: .bottom)
                }
            }
        }
    }
}

private struct OverlayGlyph: View {
    @EnvironmentObject private var viewer: Viewer
    var systemName: String
    var dimmed = false

    var body: some View {
        Image(systemName: systemName)
            .font(.system(size: 13, weight: .semibold))
            .foregroundStyle(Color(hex: viewer.palette.fg).opacity(dimmed ? 0.4 : 1))
            .frame(width: 34, height: 34)
            .background(
                Circle().fill(Color(hex: viewer.palette.surface).opacity(0.92))
            )
            .overlay(
                Circle().stroke(Color(hex: viewer.palette.muted).opacity(0.45), lineWidth: 1)
            )
    }
}

private struct OverlayIconButton: View {
    var systemName: String
    var help: String
    var dimmed = false
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            OverlayGlyph(systemName: systemName, dimmed: dimmed)
        }
        .buttonStyle(.plain)
        .help(help)
        .accessibilityLabel(help)
    }
}

#if os(macOS)
/* Native NSMenu anchored to the top of the palette button so it
 * opens upward from the bottom overlay. */
private struct ThemeDropUpButton: NSViewRepresentable {
    @EnvironmentObject var viewer: Viewer

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> ThemeDropUpView {
        let view = ThemeDropUpView()
        view.coordinator = context.coordinator
        return view
    }

    func updateNSView(_ view: ThemeDropUpView, context: Context) {
        context.coordinator.viewer = viewer
        view.apply(viewer)
    }

    final class Coordinator: NSObject {
        var viewer: Viewer?

        @objc func pick(_ sender: NSMenuItem) {
            guard let id = sender.representedObject as? String else { return }
            viewer?.selectTheme(id)
        }

        @objc func chooseFile(_ sender: NSMenuItem) {
            viewer?.chooseThemeFile()
        }
    }
}

private final class ThemeDropUpView: NSView {
    weak var coordinator: ThemeDropUpButton.Coordinator?
    private let imageView = NSImageView()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.cornerRadius = 17
        layer?.borderWidth = 1
        toolTip = "Theme"
        imageView.imageScaling = .scaleProportionallyDown
        imageView.translatesAutoresizingMaskIntoConstraints = false
        addSubview(imageView)
        NSLayoutConstraint.activate([
            widthAnchor.constraint(equalToConstant: 34),
            heightAnchor.constraint(equalToConstant: 34),
            imageView.centerXAnchor.constraint(equalTo: centerXAnchor),
            imageView.centerYAnchor.constraint(equalTo: centerYAnchor),
            imageView.widthAnchor.constraint(equalToConstant: 15),
            imageView.heightAnchor.constraint(equalToConstant: 15),
        ])
    }

    required init?(coder: NSCoder) { nil }

    override var intrinsicContentSize: NSSize { NSSize(width: 34, height: 34) }

    func apply(_ viewer: Viewer) {
        imageView.image = NSImage(
            systemSymbolName: "paintpalette",
            accessibilityDescription: "Theme"
        )
        imageView.contentTintColor = nsColor(viewer.palette.fg)
        layer?.backgroundColor = nsColor(viewer.palette.surface)
            .withAlphaComponent(0.92).cgColor
        layer?.borderColor = nsColor(viewer.palette.muted)
            .withAlphaComponent(0.45).cgColor
    }

    override func mouseDown(with event: NSEvent) {
        guard let coordinator, let viewer = coordinator.viewer else { return }
        let menu = NSMenu()
        menu.autoenablesItems = false
        func add(_ title: String, id: String, enabled: Bool = true) {
            let item = NSMenuItem(
                title: title,
                action: #selector(ThemeDropUpButton.Coordinator.pick(_:)),
                keyEquivalent: ""
            )
            item.target = coordinator
            item.representedObject = id
            item.state = viewer.themeID == id ? .on : .off
            item.isEnabled = enabled
            menu.addItem(item)
        }
        add("Default", id: ThemeCatalog.defaultID)
        for theme in ThemeCatalog.bundled {
            add(theme.name, id: theme.id)
        }
        if viewer.themeID == ThemeCatalog.customID {
            add("Custom", id: ThemeCatalog.customID, enabled: false)
        }
        menu.addItem(.separator())
        let choose = NSMenuItem(
            title: "Choose File…",
            action: #selector(ThemeDropUpButton.Coordinator.chooseFile(_:)),
            keyEquivalent: ""
        )
        choose.target = coordinator
        menu.addItem(choose)
        /* Last item at the top of this button → menu grows upward. */
        menu.popUp(
            positioning: menu.items.last,
            at: NSPoint(x: 0, y: bounds.height),
            in: self
        )
    }

    private func nsColor(_ hex: String) -> NSColor {
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
}
#endif
