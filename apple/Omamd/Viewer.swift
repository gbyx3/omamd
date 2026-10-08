#if os(macOS)
import AppKit
#endif
import Darwin
import Foundation
import UniformTypeIdentifiers

@MainActor
final class Viewer: ObservableObject {
    enum Mode {
        case preview
        case source
    }

    @Published var mode: Mode = .preview
    @Published var markdown = ""
    @Published var html = ""
    @Published var windowTitle = "omamd"
    @Published var fileURL: URL?
    @Published var palette = Omamd.palette()
    @Published var followGeneration = 0
    @Published var themeID: String
    @Published var hideTitleBar: Bool
    @Published var followEnabled: Bool
    @Published var importer: Importer?
    @Published var loadError: String?

    enum Importer {
        case markdown
        case theme
    }

    private static let themeIDKey = "omamd.themeID"
    private static let hideTitleBarKey = "omamd.hideTitleBar"
    private static let followKey = "omamd.follow"

    let fontDir = BundledFonts.directory
    private(set) var docDir: URL?
    private var fileWatch = PathWatcher()
    private var themeWatch = PathWatcher()
    private var reloadWork: DispatchWorkItem?
    private var themeWork: DispatchWorkItem?
    private var securityScoped: URL?

    init() {
        let stored = UserDefaults.standard.string(forKey: Self.themeIDKey)
        if let stored {
            themeID = stored
        } else if FileManager.default.fileExists(atPath: Omamd.userThemePath) {
            themeID = ThemeCatalog.customID
        } else {
            themeID = ThemeCatalog.defaultID
        }
        #if os(iOS)
        hideTitleBar = false
        #else
        if UserDefaults.standard.object(forKey: Self.hideTitleBarKey) == nil {
            hideTitleBar = true
        } else {
            hideTitleBar = UserDefaults.standard.bool(forKey: Self.hideTitleBarKey)
        }
        #endif
        if UserDefaults.standard.object(forKey: Self.followKey) == nil {
            followEnabled = true
        } else {
            followEnabled = UserDefaults.standard.bool(forKey: Self.followKey)
        }
        /* HTML already reads the saved colors.toml on iOS; the overlay
         * palette used to stay on the built-in default until a theme
         * was picked again. Load it before the first paint. */
        palette = Omamd.palette(themePath: resolvedThemePath)
        showWelcome()
        watchTheme()
    }

    var previewHelp: String {
        #if os(iOS)
        mode == .preview ? "Source" : "Preview"
        #else
        mode == .preview ? "Source (⌘2)" : "Preview (⌘1)"
        #endif
    }

    var markdownTypes: [UTType] {
        /* Drive and other Files providers often tag .md as public.data. */
        [
            UTType(filenameExtension: "md") ?? .plainText,
            UTType(filenameExtension: "markdown") ?? .plainText,
            UTType(filenameExtension: "mdown") ?? .plainText,
            .plainText,
            .text,
            .data,
        ]
    }

    var themeTypes: [UTType] {
        [UTType(filenameExtension: "toml") ?? .plainText]
    }

    func toggleMode() {
        mode = (mode == .preview) ? .source : .preview
    }

    func setHideTitleBar(_ hide: Bool) {
        hideTitleBar = hide
        UserDefaults.standard.set(hide, forKey: Self.hideTitleBarKey)
    }

    func setFollowEnabled(_ follow: Bool) {
        followEnabled = follow
        UserDefaults.standard.set(follow, forKey: Self.followKey)
    }

    func toggleFollow() {
        setFollowEnabled(!followEnabled)
    }

    func showWelcome() {
        fileURL = nil
        docDir = fontDir
        fileWatch.cancel()
        releaseSecurityScope()
        if let url = Bundle.main.url(forResource: "welcome", withExtension: "md"),
           let text = try? String(contentsOf: url, encoding: .utf8) {
            setDocument(markdown: text, title: "welcome.md", follow: false)
        } else {
            setDocument(
                markdown: "# omamd\n\nMissing welcome.md in the app bundle.\n",
                title: "welcome.md",
                follow: false
            )
        }
    }

    func openPanel() {
        #if os(macOS)
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = markdownTypes
        if let fileURL {
            panel.directoryURL = fileURL.deletingLastPathComponent()
        }
        guard panel.runModal() == .OK, let url = panel.url else { return }
        load(url: url, follow: false)
        #else
        importer = .markdown
        #endif
    }

    func handleIncomingURL(_ url: URL) {
        loadImported(url)
    }

    func loadImported(_ url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        if access {
            securityScoped?.stopAccessingSecurityScopedResource()
            securityScoped = url
        }
        load(url: url, follow: false)
    }

    func load(url: URL, follow: Bool) {
        let path = url.path
        guard let text = try? String(contentsOf: url, encoding: .utf8) else {
            if follow { return }
            presentError("Could not read:\n\(path)")
            return
        }
        let switching = fileURL != url
        fileURL = url
        docDir = url.deletingLastPathComponent()
        setDocument(markdown: text, title: url.lastPathComponent, follow: follow)
        /* Re-arming kqueue on every reload drops later writes; only
         * watch when opening a different file. */
        if switching {
            watchFile(url)
        }
    }

    func reload(follow: Bool) {
        guard let fileURL else { return }
        load(url: fileURL, follow: follow)
    }

    func drop(providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first else { return false }
        provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { [weak self] item, _ in
            let url: URL?
            if let value = item as? URL {
                url = value
            } else if let data = item as? Data {
                url = URL(dataRepresentation: data, relativeTo: nil)
            } else if let string = item as? String {
                url = URL(fileURLWithPath: string)
            } else {
                url = nil
            }
            guard let url else { return }
            DispatchQueue.main.async {
                self?.loadImported(url)
            }
        }
        return true
    }

    func openMarkdownLink(_ url: URL) {
        guard let docDir else { return }
        let path = url.path
        guard Omamd.isMarkdown(url: url), isUnder(path, docDir.path) else { return }
        load(url: URL(fileURLWithPath: path), follow: false)
    }

    func applyTheme() {
        let next = Omamd.palette(themePath: resolvedThemePath)
        guard next != palette else { return }
        palette = next
        render(follow: false)
    }

    func selectTheme(_ id: String) {
        if id == ThemeCatalog.customID { return }
        themeID = id
        UserDefaults.standard.set(id, forKey: Self.themeIDKey)
        let dest = URL(fileURLWithPath: Omamd.userThemePath)
        do {
            try FileManager.default.createDirectory(
                at: dest.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            if id == ThemeCatalog.defaultID {
                if FileManager.default.fileExists(atPath: dest.path) {
                    try FileManager.default.removeItem(at: dest)
                }
            } else if let theme = ThemeCatalog.bundled.first(where: { $0.id == id }) {
                if FileManager.default.fileExists(atPath: dest.path) {
                    try FileManager.default.removeItem(at: dest)
                }
                try FileManager.default.copyItem(at: theme.url, to: dest)
            }
        } catch {
            presentError(error.localizedDescription)
            return
        }
        applyTheme()
        watchTheme()
    }

    func chooseThemeFile() {
        #if os(macOS)
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = themeTypes
        panel.title = "Choose a colors.toml"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        importTheme(url)
        #else
        importer = .theme
        #endif
    }

    func importTheme(_ url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer {
            if access { url.stopAccessingSecurityScopedResource() }
        }
        let dest = URL(fileURLWithPath: Omamd.userThemePath)
        do {
            try FileManager.default.createDirectory(
                at: dest.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            if FileManager.default.fileExists(atPath: dest.path) {
                try FileManager.default.removeItem(at: dest)
            }
            try FileManager.default.copyItem(at: url, to: dest)
        } catch {
            presentError(error.localizedDescription)
            return
        }
        themeID = ThemeCatalog.customID
        UserDefaults.standard.set(themeID, forKey: Self.themeIDKey)
        applyTheme()
        watchTheme()
    }

    private var resolvedThemePath: String? {
        #if os(iOS)
        let path = Omamd.userThemePath
        return FileManager.default.fileExists(atPath: path) ? path : nil
        #else
        nil
        #endif
    }

    private func setDocument(markdown: String, title: String, follow: Bool) {
        self.markdown = markdown
        windowTitle = "\(title) — omamd"
        render(follow: follow)
    }

    private func render(follow: Bool) {
        let title = fileURL?.lastPathComponent ?? "welcome.md"
        html = Omamd.page(
            markdown: markdown,
            title: title,
            themePath: resolvedThemePath,
            fontDir: fontDir
        )
        if follow {
            followGeneration += 1
        }
    }

    private func watchFile(_ url: URL) {
        fileWatch.watch(file: url, directory: url.deletingLastPathComponent()) { [weak self] in
            self?.scheduleFileReload()
        }
    }

    private func watchTheme() {
        themeWatch.watch(paths: themeWatchPaths()) { [weak self] in
            self?.scheduleThemeReload()
        }
    }

    /* colors.toml, then ~/.config/omamd, then ~/.config so the first
     * paste of a palette is picked up without a restart. */
    private func themeWatchPaths() -> [URL] {
        let file = URL(fileURLWithPath: Omamd.userThemePath)
        let dir = file.deletingLastPathComponent()
        let config = dir.deletingLastPathComponent()
        let fm = FileManager.default
        var urls: [URL] = []
        if fm.fileExists(atPath: file.path) { urls.append(file) }
        if fm.fileExists(atPath: dir.path) {
            urls.append(dir)
        } else if fm.fileExists(atPath: config.path) {
            urls.append(config)
        }
        return urls
    }

    private func scheduleFileReload() {
        reloadWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            self?.reload(follow: self?.followEnabled ?? true)
        }
        reloadWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12, execute: work)
    }

    private func scheduleThemeReload() {
        themeWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            self?.applyTheme()
        }
        themeWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25, execute: work)
    }

    private func isUnder(_ path: String, _ dir: String) -> Bool {
        let realPath = URL(fileURLWithPath: path).resolvingSymlinksInPath().path
        let realDir = URL(fileURLWithPath: dir).resolvingSymlinksInPath().path
        return realPath == realDir || realPath.hasPrefix(realDir.hasSuffix("/") ? realDir : realDir + "/")
    }

    private func presentError(_ message: String) {
        #if os(macOS)
        NSAlert(error: NSError(
            domain: "rocks.gurra.omamd",
            code: 1,
            userInfo: [NSLocalizedDescriptionKey: message]
        )).runModal()
        #else
        loadError = message
        #endif
    }

    private func releaseSecurityScope() {
        securityScoped?.stopAccessingSecurityScopedResource()
        securityScoped = nil
    }
}

final class PathWatcher {
    private var sources: [DispatchSourceFileSystemObject] = []

    func cancel() {
        sources.forEach { $0.cancel() }
        sources.removeAll()
    }

    func watch(file: URL?, directory: URL?, handler: @escaping () -> Void) {
        var paths: [URL] = []
        if let file { paths.append(file) }
        if let directory { paths.append(directory) }
        watch(paths: paths, handler: handler)
    }

    func watch(paths: [URL], handler: @escaping () -> Void) {
        cancel()
        var seen = Set<String>()
        for url in paths {
            let path = url.path
            guard !seen.contains(path) else { continue }
            seen.insert(path)
            add(url, handler)
        }
    }

    private func add(_ url: URL, _ handler: @escaping () -> Void) {
        let fd = Darwin.open(url.path, O_EVTONLY)
        guard fd >= 0 else { return }
        let source = DispatchSource.makeFileSystemObjectSource(
            fileDescriptor: fd,
            eventMask: [.write, .extend, .attrib, .delete, .rename, .link, .revoke],
            queue: .main
        )
        source.setEventHandler { handler() }
        source.setCancelHandler { Darwin.close(fd) }
        source.resume()
        sources.append(source)
    }
}
