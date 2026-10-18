import AVFoundation
import Darwin
import UIKit

private final class MediaPlayerEntry {
    let player = AVPlayer()
    let layer = AVPlayerLayer()
    var source: URL?
    var temporary: URL?
    var observation: NSKeyValueObservation?
    var sizeObservation: NSKeyValueObservation?
    var endObserver: NSObjectProtocol?
    var failureObserver: NSObjectProtocol?
    var status: Int64 = 0
    var preparing = false
    var prepared = false
    var playing = false
    var visiblePlayback = false
    var looping = false
    var screen = false
    var completed = false
    var stopped = false

    init() { layer.player = player; layer.videoGravity = .resizeAspect }
    func dispose() {
        observation?.invalidate()
        observation = nil
        sizeObservation?.invalidate()
        sizeObservation = nil
        if let token = endObserver { NotificationCenter.default.removeObserver(token) }
        if let token = failureObserver { NotificationCenter.default.removeObserver(token) }
        endObserver = nil
        failureObserver = nil
        player.pause()
        player.replaceCurrentItem(with: nil)
        if let url = temporary { try? FileManager.default.removeItem(at: url) }
        temporary = nil
    }
}

final class KuDroidMediaPlayerBridge {
    static let shared = KuDroidMediaPlayerBridge()
    private let queue = DispatchQueue(label: "kudroid.media.player")
    private var entries: [Int64: MediaPlayerEntry] = [:]
    private var nextHandle: Int64 = 1

    func create() -> Int64 {
        queue.sync {
            let handle = nextHandle
            nextHandle += 1
            entries[handle] = MediaPlayerEntry()
            return handle
        }
    }

    // Copy exactly the descriptor range before the guest closes its descriptor.
    private func copyRange(_ fd: Int32, _ offset: Int64, _ length: Int64) throws -> URL {
        var info = stat()
        guard fd >= 0, offset >= 0, length >= -1, fstat(fd, &info) == 0,
              offset <= info.st_size else { throw CocoaError(.fileReadCorruptFile) }
        let count = length == -1 ? Int64(info.st_size) - offset : length
        guard count > 0, count <= Int64(info.st_size) - offset else {
            throw CocoaError(.fileReadCorruptFile)
        }
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("media-\(UUID().uuidString).mp4")
        guard FileManager.default.createFile(atPath: url.path, contents: nil) else {
            throw CocoaError(.fileWriteUnknown)
        }
        do {
            let output = try FileHandle(forWritingTo: url)
            defer { try? output.close() }
            var buffer = [UInt8](repeating: 0, count: 65536)
            var position: Int64 = 0
            while position < count {
                let wanted = Int(min(Int64(buffer.count), count - position))
                let readCount = buffer.withUnsafeMutableBytes {
                    pread(fd, $0.baseAddress!, wanted, off_t(offset + position))
                }
                if readCount < 0 && errno == EINTR { continue }
                guard readCount > 0 else { throw CocoaError(.fileReadCorruptFile) }
                try output.write(contentsOf: Data(buffer.prefix(readCount)))
                position += Int64(readCount)
            }
            return url
        } catch {
            try? FileManager.default.removeItem(at: url)
            throw error
        }
    }

    func source(_ handle: Int64, _ path: String?, _ fd: Int32, _ offset: Int64, _ length: Int64) -> Int32 {
        queue.sync {
            guard let entry = entries[handle], entry.source == nil, !entry.preparing else { return -1 }
            do {
                if let path = path {
                    if let url = URL(string: path), ["http", "https"].contains(url.scheme?.lowercased() ?? "") {
                        entry.source = url
                    } else {
                        guard FileManager.default.isReadableFile(atPath: path) else { return -1 }
                        entry.source = URL(fileURLWithPath: path)
                    }
                } else {
                    let url = try copyRange(fd, offset, length)
                    entry.temporary = url
                    entry.source = url
                }
                NSLog("[KuDroidMedia] MediaPlayer source handle=%lld descriptor=%d", handle, fd >= 0 ? 1 : 0)
                return 0
            } catch {
                NSLog("[KuDroidMedia] MediaPlayer source failed: %@", String(describing: error))
                return -1
            }
        }
    }

    // Called only on queue; layer mutations are ordered on the main queue.
    private func display(_ handle: Int64, _ entry: MediaPlayerEntry) {
        let layer = entry.screen && entry.visiblePlayback ? entry.layer : nil
        DispatchQueue.main.async { kudroidSetGuestVideoLayer(handle, layer) }
    }

    private func fail(_ handle: Int64, _ entry: MediaPlayerEntry, _ error: Error?) {
        guard entry.status != -1 else { return }
        entry.status = -1
        entry.playing = false
        entry.visiblePlayback = false
        entry.player.pause()
        display(handle, entry)
        NSLog("[KuDroidMedia] MediaPlayer failed handle=%lld: %@", handle, String(describing: error))
        kudroid_media_player_post(handle, 100, -1010)
    }

    private func prepare(_ handle: Int64, _ entry: MediaPlayerEntry) -> Int64 {
        guard let source = entry.source, !entry.preparing else { return -1 }
        entry.preparing = true
        entry.prepared = false
        entry.stopped = false
        entry.status = 0
        let item = AVPlayerItem(url: source)
        entry.player.replaceCurrentItem(with: item)
        entry.observation = item.observe(\.status, options: [.initial, .new]) { [weak self, weak entry] item, _ in
            self?.queue.async { [weak self, weak entry] in
                guard let self = self, let entry = entry, self.entries[handle] === entry,
                      entry.preparing, entry.player.currentItem === item else { return }
                if item.status == .readyToPlay && !entry.prepared {
                    entry.prepared = true
                    entry.status = 1
                    NSLog("[KuDroidMedia] MediaPlayer prepared handle=%lld", handle)
                    kudroid_media_player_post(handle, 1, 0)
                } else if item.status == .failed {
                    self.fail(handle, entry, item.error)
                }
            }
        }
        entry.sizeObservation = item.observe(\.presentationSize, options: [.new]) { [weak self, weak entry] item, _ in
            self?.queue.async { [weak self, weak entry] in
                guard let self = self, let entry = entry, self.entries[handle] === entry,
                      entry.preparing, entry.player.currentItem === item else { return }
                kudroid_media_player_post(handle, 5, 0)
            }
        }
        if let token = entry.endObserver { NotificationCenter.default.removeObserver(token) }
        if let token = entry.failureObserver { NotificationCenter.default.removeObserver(token) }
        entry.endObserver = NotificationCenter.default.addObserver(
            forName: .AVPlayerItemDidPlayToEndTime, object: item, queue: nil
        ) { [weak self, weak entry] _ in
            self?.queue.async { [weak self, weak entry] in
                guard let self = self, let entry = entry, self.entries[handle] === entry,
                      entry.preparing, entry.player.currentItem === item else { return }
                if entry.looping && entry.playing {
                    entry.player.seek(to: .zero)
                    entry.player.play()
                } else {
                    entry.completed = true
                    entry.playing = false
                    self.display(handle, entry)
                    kudroid_media_player_post(handle, 2, 0)
                }
            }
        }
        entry.failureObserver = NotificationCenter.default.addObserver(
            forName: .AVPlayerItemFailedToPlayToEndTime, object: item, queue: nil
        ) { [weak self, weak entry] _ in
            self?.queue.async { [weak self, weak entry] in
                guard let self = self, let entry = entry, self.entries[handle] === entry,
                      entry.preparing, entry.player.currentItem === item else { return }
                self.fail(handle, entry, item.error)
            }
        }
        return 0
    }

    private func milliseconds(_ time: CMTime) -> Int64 {
        let seconds = CMTimeGetSeconds(time)
        return seconds.isFinite && seconds > 0 ? Int64(min(seconds * 1000, Double(Int32.max))) : 0
    }

    private func dimension(_ value: CGFloat?) -> Int64 {
        guard let value = value, value.isFinite, value > 0 else { return 0 }
        return Int64(min(value, CGFloat(Int32.max)))
    }

    func command(_ handle: Int64, _ op: Int32, _ a: Double, _ b: Double) -> Int64 {
        queue.sync {
            guard let entry = entries[handle] else { return -1 }
            switch op {
            case 1: return prepare(handle, entry)
            case 2:
                guard entry.prepared, entry.status == 1 else { return -1 }
                if entry.completed { entry.player.seek(to: .zero); entry.completed = false }
                entry.playing = true
                entry.visiblePlayback = true
                entry.player.play()
                display(handle, entry)
            case 3:
                guard entry.prepared else { return -1 }
                entry.playing = false
                entry.player.pause()
            case 4:
                guard entry.prepared || entry.stopped else { return -1 }
                entry.observation?.invalidate()
                entry.sizeObservation?.invalidate()
                entry.playing = false
                entry.preparing = false
                entry.prepared = false
                entry.stopped = true
                entry.status = 0
                entry.visiblePlayback = false
                entry.player.pause()
                entry.player.seek(to: .zero)
                display(handle, entry)
            case 5:
                entry.dispose()
                entry.playing = false
                entry.screen = false
                entry.visiblePlayback = false
                display(handle, entry)
                entries.removeValue(forKey: handle)
            case 6: return milliseconds(entry.player.currentTime())
            case 7: return entry.player.currentItem.map { milliseconds($0.duration) } ?? 0
            case 8: return dimension(entry.player.currentItem?.presentationSize.width)
            case 9: return dimension(entry.player.currentItem?.presentationSize.height)
            case 10:
                guard entry.prepared, a.isFinite else { return -1 }
                entry.player.seek(to: CMTime(seconds: max(0, a) / 1000, preferredTimescale: 1000),
                                  toleranceBefore: .zero, toleranceAfter: .zero) { [weak self, weak entry] finished in
                    self?.queue.async { [weak self, weak entry] in
                        guard finished, let self = self, let entry = entry,
                              self.entries[handle] === entry, entry.prepared else { return }
                        kudroid_media_player_post(handle, 4, 0)
                    }
                }
            case 11:
                guard a.isFinite, b.isFinite else { return -1 }
                entry.player.volume = Float(max(0, min(1, (a + b) / 2)))
            case 12: entry.looping = a != 0
            case 13: entry.screen = a != 0; display(handle, entry)
            case 14: return entry.playing ? 1 : 0
            case 15: return entry.status
            default: return -1
            }
            return 0
        }
    }

    func resetAll() {
        queue.sync {
            for (handle, entry) in entries {
                entry.dispose()
                entry.playing = false
                entry.screen = false
                entry.visiblePlayback = false
                display(handle, entry)
            }
            entries.removeAll()
        }
    }
}

func registerKuDroidMediaPlayerHost() {
    var host = KuDroidMediaPlayerHost(
        create: { KuDroidMediaPlayerBridge.shared.create() },
        source: { handle, path, fd, offset, length in
            KuDroidMediaPlayerBridge.shared.source(handle, path.map { String(cString: $0) }, fd, offset, length)
        },
        command: { KuDroidMediaPlayerBridge.shared.command($0, $1, $2, $3) },
        reset_all: { KuDroidMediaPlayerBridge.shared.resetAll() }
    )
    kudroid_set_media_player_host(&host)
}
