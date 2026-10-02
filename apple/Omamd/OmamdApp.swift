import SwiftUI

@main
struct OmamdApp: App {
    init() {
        BundledFonts.register()
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
        }
        .defaultSize(width: 820, height: 960)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}
