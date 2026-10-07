import SwiftUI

@main
struct OmamdApp: App {
    @StateObject private var viewer: Viewer

    init() {
        BundledFonts.register()
        _viewer = StateObject(wrappedValue: Viewer())
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(viewer)
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
            CommandMenu("View") {
                Button("Preview") { viewer.mode = .preview }
                    .keyboardShortcut("1", modifiers: .command)
                Button("Source") { viewer.mode = .source }
                    .keyboardShortcut("2", modifiers: .command)
            }
        }
    }
}
