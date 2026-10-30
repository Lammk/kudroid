import Foundation

final class KuDroidThermalMonitor {
    static let shared = KuDroidThermalMonitor()
    private var observer: NSObjectProtocol?

    func start() {
        guard observer == nil else { return }
        observer = NotificationCenter.default.addObserver(
            forName: ProcessInfo.thermalStateDidChangeNotification, object: nil, queue: nil
        ) { [weak self] _ in self?.publish() }
        publish()
    }

    private func publish() {
        let status: Int32
        switch ProcessInfo.processInfo.thermalState {
        case .nominal: status = 0
        case .fair: status = 1
        case .serious: status = 3
        case .critical: status = 4
        @unknown default: status = -1
        }
        kudroid_set_thermal_status(status)
    }

    deinit {
        if let observer { NotificationCenter.default.removeObserver(observer) }
    }
}
