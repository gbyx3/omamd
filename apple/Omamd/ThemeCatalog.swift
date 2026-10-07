import Foundation

struct BundledTheme: Identifiable, Hashable {
    var id: String
    var name: String
    var url: URL
}

enum ThemeCatalog {
    static let defaultID = "default"
    static let customID = "custom"

    static var bundled: [BundledTheme] {
        guard let dir = Bundle.main.url(forResource: "themes", withExtension: nil) else {
            return []
        }
        let urls = (try? FileManager.default.contentsOfDirectory(
            at: dir,
            includingPropertiesForKeys: nil
        )) ?? []
        return urls
            .filter { $0.pathExtension.lowercased() == "toml" }
            .map { url in
                let id = url.deletingPathExtension().lastPathComponent
                return BundledTheme(id: id, name: displayName(id), url: url)
            }
            .sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    static func displayName(_ id: String) -> String {
        id.split(separator: "-")
            .map { part in
                if part.allSatisfy(\.isNumber) { return String(part) }
                return part.prefix(1).uppercased() + part.dropFirst()
            }
            .joined(separator: " ")
    }
}
