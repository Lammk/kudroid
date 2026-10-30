import SwiftUI
import UIKit
import Combine
import ImageIO

private struct PreparedDocument {
    let directory: URL
    let archive: URL
    let name: String
    let icon: UIImage?
}

final class ExternalAPKImportModel: ObservableObject {
    @Published var presented = false
    @Published private(set) var preparing = false
    @Published private(set) var name = ""
    @Published private(set) var icon: UIImage?
    @Published var error: String?
    @Published private(set) var canConfirm = false
    private var prepared: PreparedDocument?
    private var generation = UUID()
    private var progress: Progress?
    private let worker = DispatchQueue(label: "kudroid.document-import", qos: .userInitiated)

    func open(_ url: URL) {
        guard !presented, !APKInstallModel.shared.busy else { return }
        let access = url.startAccessingSecurityScopedResource()
        let current = UUID()
        let progress = Progress(totalUnitCount: 1)
        self.progress = progress
        generation = current
        presented = true; preparing = true; canConfirm = false; error = nil; name = ""; icon = nil
        let locale = Locale.preferredLanguages.first ?? ""
        worker.async {
            defer { if access { url.stopAccessingSecurityScopedResource() } }
            guard !progress.isCancelled else { return }
            let fm = FileManager.default
            let work = fm.temporaryDirectory.appendingPathComponent("DocumentImport-" + UUID().uuidString)
            do {
                try fm.createDirectory(at: work, withIntermediateDirectories: true)
                let local = work.appendingPathComponent("source." + url.pathExtension.lowercased())
                var coordinationError: NSError?
                var copyError: Error?
                NSFileCoordinator().coordinate(readingItemAt: url, options: [], error: &coordinationError) { source in
                    do { try fm.copyItem(at: source, to: local) }
                    catch { copyError = error }
                }
                if let error = coordinationError { throw error }
                if let error = copyError { throw error }
                if progress.isCancelled { try? fm.removeItem(at: work); return }
                let metadata = work.appendingPathComponent("Preview", isDirectory: true)
                var failure: UnsafeMutablePointer<CChar>?
                let result = kudroid_preview_apk(local.path, metadata.path, locale, &failure)
                let text = failure.map { String(cString: $0) }
                if let failure = failure { free(failure) }
                guard result == 0 else { throw ExportFailure(text ?? "Could not read APK metadata") }
                let label = String(try String(contentsOf: metadata.appendingPathComponent("label.txt"), encoding: .utf8)
                    .trimmingCharacters(in: .whitespacesAndNewlines).prefix(200))
                let package = try String(contentsOf: metadata.appendingPathComponent("package.txt"), encoding: .utf8)
                guard InstalledAppStorage.validID(package) else { throw ExportFailure("Invalid Android package ID") }
                let archive = work.appendingPathComponent(package + "." + url.pathExtension.lowercased())
                try fm.moveItem(at: local, to: archive)
                let document = PreparedDocument(directory: work, archive: archive,
                    name: label.isEmpty ? url.deletingPathExtension().lastPathComponent : label,
                    icon: Self.previewIcon(metadata.appendingPathComponent("app_icon.png")))
                DispatchQueue.main.async {
                    guard self.generation == current else {
                        self.worker.async { try? fm.removeItem(at: work) }
                        return
                    }
                    self.prepared = document; self.name = document.name; self.icon = document.icon
                    self.preparing = false; self.canConfirm = true
                }
            } catch {
                try? fm.removeItem(at: work)
                DispatchQueue.main.async {
                    guard self.generation == current else { return }
                    self.preparing = false; self.error = error.localizedDescription
                }
            }
        }
    }

    private static func previewIcon(_ url: URL) -> UIImage? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL,
                [kCGImageSourceShouldCache: false] as CFDictionary),
              let image = CGImageSourceCreateThumbnailAtIndex(source, 0, [
                kCGImageSourceCreateThumbnailFromImageAlways: true,
                kCGImageSourceCreateThumbnailWithTransform: true,
                kCGImageSourceThumbnailMaxPixelSize: 256
              ] as CFDictionary) else { return nil }
        return UIImage(cgImage: image)
    }

    func cancel() {
        progress?.cancel()
        progress = nil
        generation = UUID()
        if let prepared = prepared { worker.async { try? FileManager.default.removeItem(at: prepared.directory) } }
        prepared = nil; preparing = false; canConfirm = false; presented = false; icon = nil
    }

    func confirm() {
        guard let document = prepared, !preparing, !APKInstallModel.shared.busy else { return }
        prepared = nil; generation = UUID(); canConfirm = false; presented = false; icon = nil
        APKInstallModel.shared.install(document.archive) {
            self.worker.async { try? FileManager.default.removeItem(at: document.directory) }
        }
    }
}

struct ExternalAPKImportView: View {
    @ObservedObject var model: ExternalAPKImportModel
    let installationAllowed: () -> Bool
    var body: some View {
        VStack(spacing: 24) {
            Spacer()
            if model.preparing {
                ProgressView()
                Text("Reading app information…")
            } else if model.canConfirm {
                if let icon = model.icon {
                    Image(uiImage: icon).resizable().scaledToFit().frame(width: 88, height: 88).cornerRadius(20)
                } else {
                    Image(systemName: "app.fill").font(.system(size: 72)).foregroundColor(.primary)
                }
                Text("Do you want to install \(model.name)?")
                    .font(.title3.weight(.semibold)).multilineTextAlignment(.center)
            }
            if let error = model.error { Text(error).foregroundColor(.red).multilineTextAlignment(.center) }
            HStack(spacing: 24) {
                Button("No", role: .cancel) { model.cancel() }
                    .frame(minWidth: 90)
                if model.canConfirm {
                    Button("Yes") {
                        guard installationAllowed() else {
                            model.error = "Close the running app and wait for any file operation to finish first."
                            return
                        }
                        model.confirm()
                    }.frame(minWidth: 90).buttonStyle(.borderedProminent).tint(.green)
                }
            }
            Spacer()
        }.padding(28)
    }
}
