import SwiftUI
#if os(macOS)
import AppKit
#endif

@main
struct OmamdApp: App {
    @StateObject private var viewer: Viewer

    init() {
        BundledFonts.register()
        #if os(macOS)
        /* Native window tabs are extra NSWindows. Yabai sees a hide/show
         * on each tab switch and retiles (#7). */
        NSWindow.allowsAutomaticWindowTabbing = false
        /* System View menu still adds Enter Full Screen; Window already
         * has zoom / tile / the green button. */
        MenuPruner.install()
        #endif
        _viewer = StateObject(wrappedValue: Viewer())
    }

    var body: some Scene {
        #if os(macOS)
        WindowGroup {
            ContentView()
                .environmentObject(viewer)
                .onOpenURL(perform: viewer.handleIncomingURL)
        }
        .defaultSize(width: 820, height: 960)
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandGroup(after: .newItem) {
                Button("Open…") { viewer.openPanel() }
                    .keyboardShortcut("o", modifiers: .command)
                Button("Reload") { viewer.reload(follow: false) }
                    .keyboardShortcut("r", modifiers: .command)
                    .disabled(viewer.fileURL == nil)
            }
            CommandGroup(replacing: .toolbar) {
                Button("Preview") { viewer.mode = .preview }
                    .keyboardShortcut("1", modifiers: .command)
                Button("Source") { viewer.mode = .source }
                    .keyboardShortcut("2", modifiers: .command)
                Button(viewer.hideTitleBar ? "Show Title Bar" : "Hide Title Bar") {
                    viewer.setHideTitleBar(!viewer.hideTitleBar)
                }
                Divider()
                Picker("Theme", selection: themeBinding) {
                    Text("Default").tag(ThemeCatalog.defaultID)
                    ForEach(ThemeCatalog.bundled) { theme in
                        Text(theme.name).tag(theme.id)
                    }
                    if viewer.themeID == ThemeCatalog.customID {
                        Text("Custom").tag(ThemeCatalog.customID)
                    }
                }
            }
        }
        Settings {
            SettingsView()
                .environmentObject(viewer)
        }
        #else
        WindowGroup {
            ContentView()
                .environmentObject(viewer)
                .onOpenURL(perform: viewer.handleIncomingURL)
        }
        #endif
    }

    #if os(macOS)
    private enum MenuPruner {
        static var stripping = false

        static func install() {
            let nc = NotificationCenter.default
            let strip = { Self.stripFullScreen(from: NSApp.mainMenu?.item(withTitle: "View")?.submenu) }
            nc.addObserver(forName: NSApplication.didBecomeActiveNotification, object: nil, queue: .main) { _ in
                strip()
            }
            nc.addObserver(forName: NSMenu.didBeginTrackingNotification, object: nil, queue: .main) { note in
                Self.stripFullScreen(from: note.object as? NSMenu)
                strip()
            }
            DispatchQueue.main.async(execute: strip)
        }

        static func stripFullScreen(from menu: NSMenu?) {
            guard !stripping, let menu else { return }
            stripping = true
            defer { stripping = false }
            for item in menu.items {
                let mods = item.keyEquivalentModifierMask
                let isFullScreen =
                    item.action == #selector(NSWindow.toggleFullScreen(_:))
                    || item.title.localizedCaseInsensitiveContains("Full Screen")
                    || (item.keyEquivalent.lowercased() == "f"
                        && mods.contains(.command)
                        && mods.contains(.control))
                if isFullScreen {
                    item.isHidden = true
                    menu.removeItem(item)
                }
            }
            while menu.items.last?.isSeparatorItem == true {
                menu.removeItem(menu.items.last!)
            }
        }
    }

    private var themeBinding: Binding<String> {
        Binding(
            get: { viewer.themeID },
            set: { viewer.selectTheme($0) }
        )
    }
    #endif
}
