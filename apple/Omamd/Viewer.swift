import AppKit
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

    private static let themeIDKey = "omamd.themeID"

    let fontDir = BundledFonts.directory
    private(set) var docDir: URL?
    private var fileWatch = PathWatcher()
    private var themeWatch = PathWatcher()
    private var reloadWork: DispatchWorkItem?
    private var themeWork: DispatchWorkItem?

    init() {
        let stored = UserDefaults.standard.string(forKey: Self.themeIDKey)
        if let stored {
            themeID = stored
        } else if FileManager.default.fileExists(atPath: Omamd.userThemePath) {
            themeID = ThemeCatalog.customID
        } else {
            themeID = ThemeCatalog.defaultID
        }
        showWelcome()
        watchTheme()
    }

    var previewHelp: String {
        mode == .preview ? "Source (⌘2)" : "Preview (⌘1)"
    }

    func toggleMode() {
        mode = (mode == .preview) ? .source : .preview
    }

    func showWelcome() {
        fileURL = nil
        docDir = fontDir
        fileWatch.cancel()
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
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = [
            UTType(filenameExtension: "md") ?? .plainText,
            UTType(filenameExtension: "markdown") ?? .plainText,
            UTType(filenameExtension: "mdown") ?? .plainText,
            .plainText,
        ]
        if let fileURL {
            panel.directoryURL = fileURL.deletingLastPathComponent()
        }
        guard panel.runModal() == .OK, let url = panel.url else { return }
        load(url: url, follow: false)
    }

    func load(url: URL, follow: Bool) {
        let path = url.path
        guard let text = try? String(contentsOf: url, encoding: .utf8) else {
            if follow { return }
            NSAlert(error: NSError(
                domain: "rocks.gurra.omamd",
                code: 1,
                userInfo: [NSLocalizedDescriptionKey: "Could not read:\n\(path)"]
            )).runModal()
            return
        }
        fileURL = url
        docDir = url.deletingLastPathComponent()
        setDocument(markdown: text, title: url.lastPathComponent, follow: follow)
        watchFile(url)
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
                self?.load(url: url, follow: false)
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
        let next = Omamd.palette()
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
            NSAlert(error: error).runModal()
            return
        }
        applyTheme()
        watchTheme()
    }

    func chooseThemeFile() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = [UTType(filenameExtension: "toml") ?? .plainText]
        panel.title = "Choose a colors.toml"
        guard panel.runModal() == .OK, let url = panel.url else { return }
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
            NSAlert(error: error).runModal()
            return
        }
        themeID = ThemeCatalog.customID
        UserDefaults.standard.set(themeID, forKey: Self.themeIDKey)
        applyTheme()
        watchTheme()
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
            guard let self else { return }
            self.reload(follow: true)
            if let fileURL {
                self.watchFile(fileURL)
            }
        }
        reloadWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12, execute: work)
    }

    private func scheduleThemeReload() {
        themeWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            self?.applyTheme()
            self?.watchTheme()
        }
        themeWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25, execute: work)
    }

    private func isUnder(_ path: String, _ dir: String) -> Bool {
        let realPath = URL(fileURLWithPath: path).resolvingSymlinksInPath().path
        let realDir = URL(fileURLWithPath: dir).resolvingSymlinksInPath().path
        return realPath == realDir || realPath.hasPrefix(realDir.hasSuffix("/") ? realDir : realDir + "/")
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
