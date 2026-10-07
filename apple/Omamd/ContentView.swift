import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @EnvironmentObject private var viewer: Viewer

    var body: some View {
        ZStack(alignment: .topTrailing) {
            preview
                .opacity(viewer.mode == .preview ? 1 : 0)
                .allowsHitTesting(viewer.mode == .preview)
            source
                .opacity(viewer.mode == .source ? 1 : 0)
                .allowsHitTesting(viewer.mode == .source)

            Button(action: viewer.toggleMode) {
                Image(systemName: viewer.mode == .preview
                      ? "chevron.left.forwardslash.chevron.right"
                      : "eye")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(Color(hex: viewer.palette.fg))
                    .frame(width: 34, height: 34)
                    .background(
                        Circle().fill(Color(hex: viewer.palette.surface).opacity(0.92))
                    )
                    .overlay(
                        Circle().stroke(Color(hex: viewer.palette.muted).opacity(0.45), lineWidth: 1)
                    )
            }
            .buttonStyle(.plain)
            .help(viewer.previewHelp)
            .padding(14)
        }
        .background(Color(hex: viewer.palette.bg))
        .background(WindowTitleSetter(title: viewer.windowTitle))
        .frame(minWidth: 520, minHeight: 640)
        .onDrop(of: [.fileURL], isTargeted: nil, perform: viewer.drop)
    }

    private var preview: some View {
        MarkdownWebView(
            html: viewer.html,
            baseURL: viewer.docDir,
            docDir: viewer.docDir,
            followGeneration: UInt(viewer.followGeneration),
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
