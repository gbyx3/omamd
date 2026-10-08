import SwiftUI

struct SettingsView: View {
    @EnvironmentObject private var viewer: Viewer

    var body: some View {
        Form {
            Section("Theme") {
                Picker("Palette", selection: themeBinding) {
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
            }
            #if os(macOS)
            Section("Window") {
                HStack {
                    Text("Opacity")
                    Slider(value: opacityBinding, in: Viewer.opacityRange)
                    Text("\(Int((viewer.windowOpacity * 100).rounded()))%")
                        .monospacedDigit()
                        .frame(width: 40, alignment: .trailing)
                        .foregroundStyle(.secondary)
                }
                Toggle("Hide title bar", isOn: hideTitleBarBinding)
            }
            #endif
            Section("Reading") {
                Toggle("Follow file changes", isOn: followBinding)
            }
        }
        #if os(macOS)
        .formStyle(.grouped)
        .frame(minWidth: 420, minHeight: 320)
        #endif
    }

    private var themeBinding: Binding<String> {
        Binding(
            get: { viewer.themeID },
            set: { viewer.selectTheme($0) }
        )
    }

    private var followBinding: Binding<Bool> {
        Binding(
            get: { viewer.followEnabled },
            set: { viewer.setFollowEnabled($0) }
        )
    }

    #if os(macOS)
    private var hideTitleBarBinding: Binding<Bool> {
        Binding(
            get: { viewer.hideTitleBar },
            set: { viewer.setHideTitleBar($0) }
        )
    }

    private var opacityBinding: Binding<Double> {
        Binding(
            get: { viewer.windowOpacity },
            set: { viewer.setWindowOpacity($0) }
        )
    }
    #endif
}
