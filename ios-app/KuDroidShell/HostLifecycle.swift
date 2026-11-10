import SwiftUI
import UIKit
import AVFAudio
import AVFoundation
import Metal
import QuartzCore
import CoreMotion
import Combine
import UniformTypeIdentifiers

private final class AudioSessionLifecycle {
    static let shared = AudioSessionLifecycle()
    private var observers: [NSObjectProtocol] = []
    private var inactive = false
    private var interrupted = false

    private init() {
        let center = NotificationCenter.default
        observers = [
            center.addObserver(forName: UIApplication.willResignActiveNotification,
                               object: nil, queue: .main) { [weak self] _ in
                self?.inactive = true
                kudroid_audio_session_suspend()
            },
            center.addObserver(forName: UIApplication.didBecomeActiveNotification,
                               object: nil, queue: .main) { [weak self] _ in
                guard let self = self else { return }
                self.inactive = false
                // A suspended process may never receive the interruption end event.
                self.interrupted = false
                self.resume()
            },
            center.addObserver(forName: AVAudioSession.interruptionNotification,
                               object: AVAudioSession.sharedInstance(), queue: .main) { [weak self] notification in
                guard let self = self,
                      let value = notification.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
                      let type = AVAudioSession.InterruptionType(rawValue: value) else { return }
                switch type {
                case .began:
                    self.interrupted = true
                    kudroid_audio_session_suspend()
                case .ended:
                    self.interrupted = false
                    self.resume()
                @unknown default:
                    break
                }
            }
        ]
    }

    @discardableResult
    func activate() -> Bool {
        guard !inactive && !interrupted else { return false }
        let session = AVAudioSession.sharedInstance()
        do {
            if kudroid_audio_capture_active() > 0 {
                // Bluetooth HFP option (0x4): the enum case was renamed across SDKs, so use the raw value.
                let bluetoothHFP = AVAudioSession.CategoryOptions(rawValue: 0x4)
                try session.setCategory(.playAndRecord, mode: .default,
                                        options: [.mixWithOthers, bluetoothHFP, .defaultToSpeaker])
            } else {
                try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            }
            try session.setActive(true)
            return true
        } catch {
            print("[KuDroidAudio] session activation failed: \(error)")
            return false
        }
    }

    private func resume() {
        guard activate() else { return }
        kudroid_audio_session_resume()
    }
}

func activateAudioSession() {
    AudioSessionLifecycle.shared.activate()
}

/// Publish the OS authorization of the sensors a guest can ask for, so the guest's
/// permission checks reflect what iOS will actually allow.
func refreshHostAuthorizations() {
    switch AVAudioSession.sharedInstance().recordPermission {
    case .granted: kudroid_set_host_authorization(0, 1)
    case .denied: kudroid_set_host_authorization(0, 2)
    default: kudroid_set_host_authorization(0, 0)
    }
    switch AVCaptureDevice.authorizationStatus(for: .video) {
    case .authorized: kudroid_set_host_authorization(1, 1)
    case .notDetermined: kudroid_set_host_authorization(1, 0)
    default: kudroid_set_host_authorization(1, 2)
    }
}

/// Ask iOS for the sensors named in `permissions`, then publish the answers.
func requestHostAuthorizations(for permissions: [String], completion: @escaping () -> Void) {
    let pending = DispatchGroup()
    if permissions.contains("android.permission.RECORD_AUDIO") {
        pending.enter()
        AVAudioSession.sharedInstance().requestRecordPermission { granted in
            kudroid_set_host_authorization(0, granted ? 1 : 2)
            pending.leave()
        }
    }
    if permissions.contains("android.permission.CAMERA") {
        pending.enter()
        AVCaptureDevice.requestAccess(for: .video) { granted in
            kudroid_set_host_authorization(1, granted ? 1 : 2)
            pending.leave()
        }
    }
    pending.notify(queue: .main, execute: completion)
}

/// Read the bundle display version.
func appVersion() -> String {
    if let v = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String {
        return v
    }
    return "0.0.0"
}

func setupLogDir() {
    // Resolved through the process's container, never hardcoded.
    //
    // This is what makes KuDroid work unchanged inside LiveContainer, which runs a
    // guest app as a dylib in its own process and rewrites HOME/CFFIXED_USER_HOME to
    // a nested per-guest container before loading it. `urls(for: .documentDirectory)`
    // goes through those variables, so it yields the guest's own Documents in both
    // deployments — and every guest path (android_root, the extracted APK, the .so
    // files) hangs off this one value.
    if let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
        kudroid_set_log_dir(docs.path)
        kudroid_set_documents_dir(docs.path)
        let legacy = docs.appendingPathComponent("put_apk_here", isDirectory: true)
        if FileManager.default.fileExists(atPath: legacy.path) {
            do {
                if try FileManager.default.contentsOfDirectory(atPath: legacy.path).isEmpty {
                    try FileManager.default.removeItem(at: legacy)
                } else {
                    let archive = docs.appendingPathComponent("APK Archive-" + UUID().uuidString, isDirectory: true)
                    try FileManager.default.moveItem(at: legacy, to: archive)
                }
            } catch { NSLog("APK folder migration failed: %@", error.localizedDescription) }
        }
    }
}

