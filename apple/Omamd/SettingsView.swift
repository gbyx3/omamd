import SwiftUI

struct SettingsView: View {
    @EnvironmentObject private var viewer: Viewer

    var body: some View {
        Form {
            Picker("Theme", selection: themeBinding) {
                Text("Default").tag(ThemeCatalog.defaultID)
                ForEach(ThemeCatalog.bundled) { theme in
                    Text(theme.name).tag(theme.id)
                }
                if viewer.themeID == ThemeCatalog.customID {
                    Text("Custom").tag(ThemeCatalog.customID)
                }
            }
            Button("Choose File…") {
                viewer.chooseThemeFile()
            }
            Toggle("Hide title bar", isOn: hideTitleBarBinding)
        }
        .frame(minWidth: 360)
        .padding()
    }

    private var themeBinding: Binding<String> {
        Binding(
            get: { viewer.themeID },
            set: { viewer.selectTheme($0) }
        )
    }

    private var hideTitleBarBinding: Binding<Bool> {
        Binding(
            get: { viewer.hideTitleBar },
            set: { viewer.setHideTitleBar($0) }
        )
    }
}
