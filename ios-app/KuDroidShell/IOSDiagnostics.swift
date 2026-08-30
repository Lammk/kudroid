import Foundation
import MetricKit
import OSLog
import UIKit

/// iOS delivers watchdog, jetsam and crash diagnostics after termination. This
/// subscriber persists the payload immediately so the next launch can expose it.
final class IOSDiagnostics: NSObject, MXMetricManagerSubscriber {
    static let shared = IOSDiagnostics()

    private let logger = Logger(subsystem: "com.kudroid.shell", category: "diagnostics")
    private let signposter = OSSignposter(subsystem: "com.kudroid.shell", category: "lifecycle")
    private var heartbeat: DispatchSourceTimer?
    private var sequence: UInt64 = 0

    func start() {
        MXMetricManager.shared.add(self)
        record("diagnostics-start")
        startHeartbeat()
    }

    func didReceive(_ payloads: [MXDiagnosticPayload]) {
        for payload in payloads {
            let text = "metric-kit diagnostic received: \(String(describing: payload))"
            persist(text)
            logger.error("\(text, privacy: .public)")
        }
    }

    private func diagnosticsURL() -> URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory,
                                             in: .userDomainMask)[0]
        try? FileManager.default.createDirectory(at: base,
                                                 withIntermediateDirectories: true)
        return base.appendingPathComponent("kudroid_ios_diagnostics.log")
    }

    private func persist(_ text: String) {
        let line = "\(ISO8601DateFormatter().string(from: Date())) \(text)\n"
        let url = diagnosticsURL()
        if let data = line.data(using: .utf8) {
            if FileManager.default.fileExists(atPath: url.path),
               let handle = try? FileHandle(forWritingTo: url) {
                try? handle.seekToEnd()
                try? handle.write(contentsOf: data)
                try? handle.close()
            } else {
                try? data.write(to: url, options: .atomic)
            }
        }
    }

    private func record(_ phase: String) {
        persist("phase=\(phase) pid=\(ProcessInfo.processInfo.processIdentifier)")
        kudroid_ios_diagnostic_phase(phase)
        kudroid_ios_diagnostic_memory(phase)
    }

    private func startHeartbeat() {
        let timer = DispatchSource.makeTimerSource(queue: DispatchQueue.global(qos: .utility))
        timer.schedule(deadline: .now() + .milliseconds(250), repeating: .seconds(1))
        timer.setEventHandler { [weak self] in
            guard let self else { return }
            self.sequence &+= 1
            let seq = self.sequence
            self.record("ios-watchdog-dispatch seq=\(seq)")
            DispatchQueue.main.async { [weak self] in
                self?.record("main-thread-heartbeat seq=\(seq)")
            }
        }
        timer.resume()
        heartbeat = timer
    }

    func applicationPhase(_ phase: String) {
        record(phase)
        signposter.emitEvent("phase", "\(phase, privacy: .public)")
    }

    func beginInterval(_ name: StaticString) -> OSSignpostIntervalState {
        signposter.beginInterval(name)
    }

    func endInterval(_ name: StaticString, state: OSSignpostIntervalState) {
        signposter.endInterval(name, state)
    }
}
