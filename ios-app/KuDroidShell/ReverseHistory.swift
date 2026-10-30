import SwiftUI
import UIKit
import Combine

struct ReverseExportMetadata: Codable {
    let format: Int
    let sourceAPK: String
    let package: String
    let version: String
    let created: Date

    func write(to directory: URL) throws {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        try encoder.encode(self).write(to: directory.appendingPathComponent("export.json"), options: .atomic)
    }

    static func load(from directory: URL) -> Self? {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        let file = directory.appendingPathComponent("export.json")
        guard let values = try? file.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey]),
              values.isRegularFile == true, values.isSymbolicLink != true,
              let size = values.fileSize, size >= 0, size <= 65536,
              let data = try? Data(contentsOf: file),
              let metadata = try? decoder.decode(Self.self, from: data), metadata.format == 1 else { return nil }
        return metadata
    }
}

struct ReverseHistoryItem: Identifiable {
    let url: URL
    let created: Date
    let size: Int64
    let metadata: ReverseExportMetadata?
    var id: String { url.path }
    var name: String { url.deletingPathExtension().lastPathComponent }
}

final class ReverseHistoryModel: ObservableObject {
    @Published private(set) var items: [ReverseHistoryItem] = []
    @Published private(set) var busy = false
    @Published var error: String?
    private let worker = DispatchQueue(label: "kudroid.reverse.history", qos: .userInitiated)

    static var directory: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Reverse", isDirectory: true)
    }

    private static func scan() throws -> [ReverseHistoryItem] {
        let fm = FileManager.default
        guard fm.fileExists(atPath: directory.path) else { return [] }
        guard try directory.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else {
            throw ExportFailure("Invalid export folder")
        }
        let keys: Set<URLResourceKey> = [.isDirectoryKey, .isRegularFileKey, .isSymbolicLinkKey,
                                       .fileSizeKey, .creationDateKey, .contentModificationDateKey]
        let children = try fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: Array(keys), options: [.skipsHiddenFiles])
        var files: [URL] = []
        for child in children {
            let values = try child.resourceValues(forKeys: keys)
            guard values.isSymbolicLink != true else { continue }
            if values.isDirectory == true {
                files.append(contentsOf: try fm.contentsOfDirectory(at: child, includingPropertiesForKeys: Array(keys), options: [.skipsHiddenFiles]))
            } else { files.append(child) }
        }
        var result: [ReverseHistoryItem] = []
        for file in files where file.pathExtension.lowercased() == "ipa" {
            let values = try file.resourceValues(forKeys: keys)
            guard values.isRegularFile == true, values.isSymbolicLink != true else { continue }
            let metadata = file.deletingLastPathComponent() == directory ? nil
                : ReverseExportMetadata.load(from: file.deletingLastPathComponent())
            result.append(ReverseHistoryItem(url: file,
                created: metadata?.created ?? values.creationDate ?? values.contentModificationDate ?? .distantPast,
                size: Int64(values.fileSize ?? 0), metadata: metadata))
        }
        return result.sorted { $0.created == $1.created ? $0.id < $1.id : $0.created > $1.created }
    }

    private func perform(_ operation: @escaping () throws -> Void) {
        guard !busy else { return }
        busy = true
        error = nil
        worker.async {
            var problem: String?
            do { try operation() } catch { problem = error.localizedDescription }
            var items: [ReverseHistoryItem]?
            do { items = try Self.scan() } catch { problem = problem ?? error.localizedDescription }
            let snapshot = items
            let message = problem
            DispatchQueue.main.async {
                if let snapshot = snapshot { self.items = snapshot }
                self.error = message
                self.busy = false
            }
        }
    }

    func reload() { perform {} }

    private static func checkOwned(_ url: URL) throws {
        let fm = FileManager.default
        let root = directory.resolvingSymlinksInPath().standardizedFileURL
        let file = url.resolvingSymlinksInPath().standardizedFileURL
        guard file.path.hasPrefix(root.path + "/"), file.pathExtension.lowercased() == "ipa",
              try url.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else {
            throw ExportFailure("Invalid exported file")
        }
        let parent = url.deletingLastPathComponent()
        if parent != directory,
           try parent.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink == true {
            throw ExportFailure("Invalid export folder")
        }
        guard fm.fileExists(atPath: url.path) else { throw ExportFailure("The IPA file no longer exists") }
    }

    func rename(_ item: ReverseHistoryItem, to requested: String) {
        perform {
            try Self.checkOwned(item.url)
            var name = requested.trimmingCharacters(in: .whitespacesAndNewlines)
            if name.lowercased().hasSuffix(".ipa") { name = String(name.dropLast(4)) }
            let forbidden = CharacterSet(charactersIn: "/\\:\0").union(.controlCharacters)
            guard !name.isEmpty, name != ".", name != "..", name.count <= 120,
                  name.rangeOfCharacter(from: forbidden) == nil else { throw ExportFailure("Enter a valid file name") }
            let target = item.url.deletingLastPathComponent().appendingPathComponent(name + ".ipa")
            if target == item.url { return }
            guard !FileManager.default.fileExists(atPath: target.path) else { throw ExportFailure("An IPA with this name already exists") }
            try FileManager.default.moveItem(at: item.url, to: target)
        }
    }

    private static func removeFile(_ item: ReverseHistoryItem) throws {
        try checkOwned(item.url)
        let fm = FileManager.default
        try fm.removeItem(at: item.url)
        let parent = item.url.deletingLastPathComponent()
        guard parent != directory else { return }
        var remaining = try fm.contentsOfDirectory(atPath: parent.path)
        if remaining == ["export.json"], ReverseExportMetadata.load(from: parent) != nil {
            try fm.removeItem(at: parent.appendingPathComponent("export.json"))
            remaining = []
        }
        if remaining.isEmpty { try fm.removeItem(at: parent) }
    }

    func remove(_ item: ReverseHistoryItem) { perform { try Self.removeFile(item) } }

    func removeAll() {
        perform {
            var failure: Error?
            for item in try Self.scan() {
                do { try Self.removeFile(item) } catch { failure = failure ?? error }
            }
            if let failure = failure { throw failure }
        }
    }
}

struct ReverseHistoryView: View {
    @Environment(\.dismiss) private var dismiss
    @StateObject private var model = ReverseHistoryModel()
    @State private var sharing: ReverseHistoryItem?
    @State private var renaming: ReverseHistoryItem?
    @State private var proposedName = ""
    @State private var showRename = false
    @State private var showDeleteAll = false

    var body: some View {
        NavigationView {
            List {
                if model.busy { ProgressView().frame(maxWidth: .infinity) }
                if model.items.isEmpty && !model.busy {
                    VStack(spacing: 12) {
                        Image(systemName: "folder").font(.largeTitle)
                        Text("No exported IPAs yet")
                    }.foregroundColor(.secondary).frame(maxWidth: .infinity).padding()
                }
                ForEach(model.items, id: \.id) { item in
                    HStack(spacing: 12) {
                        Image(systemName: "shippingbox.fill").foregroundColor(.primary)
                        VStack(alignment: .leading, spacing: 4) {
                            Text(item.name).font(.headline).lineLimit(2)
                            if let metadata = item.metadata {
                                Text(metadata.sourceAPK).font(.caption).foregroundColor(.secondary).lineLimit(1)
                                Text("v" + metadata.version).font(.caption2).foregroundColor(.secondary)
                            }
                            Text(item.created.formatted(date: .abbreviated, time: .shortened) + " · " +
                                 ByteCountFormatter.string(fromByteCount: item.size, countStyle: .file))
                                .font(.caption2).foregroundColor(.secondary)
                        }
                        Spacer()
                        Menu {
                            Button { sharing = item } label: { Label("Share", systemImage: "square.and.arrow.up") }
                            Button {
                                renaming = item
                                proposedName = item.name
                                showRename = true
                            } label: { Label("Rename", systemImage: "pencil") }
                            Button(role: .destructive) { model.remove(item) } label: { Label("Delete", systemImage: "trash") }
                        } label: {
                            Image(systemName: "ellipsis.circle").padding(8)
                        }.accessibilityLabel("Actions for " + item.name)
                    }.disabled(model.busy)
                }
            }
            .navigationTitle("Exported IPAs")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Close") { dismiss() }.disabled(model.busy) }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button(role: .destructive) { showDeleteAll = true } label: { Image(systemName: "trash") }
                        .accessibilityLabel("Delete All").disabled(model.items.isEmpty || model.busy)
                }
            }
            .onAppear { model.reload() }
            .onReceive(NotificationCenter.default.publisher(for: UIApplication.didBecomeActiveNotification)) { _ in
                if sharing == nil { model.reload() }
            }
            .alert("Rename IPA", isPresented: $showRename) {
                TextField("File name", text: $proposedName)
                Button("Cancel", role: .cancel) { renaming = nil }
                Button("Save") {
                    if let item = renaming { model.rename(item, to: proposedName) }
                    renaming = nil
                }
            } message: { Text("Changes the saved file name.") }
            .confirmationDialog("Delete all exported IPAs?", isPresented: $showDeleteAll, titleVisibility: .visible) {
                Button("Delete All", role: .destructive) { model.removeAll() }
            } message: { Text("This removes all saved IPA exports.") }
            .alert("Could not update exports", isPresented: Binding(get: { model.error != nil }, set: { if !$0 { model.error = nil } })) {
                Button("OK", role: .cancel) { model.error = nil }
            } message: { Text(model.error ?? "") }
        }
        .navigationViewStyle(.stack)
        .sheet(item: $sharing, onDismiss: { model.reload() }) { item in IPAActivitySheet(url: item.url) }
    }
}
