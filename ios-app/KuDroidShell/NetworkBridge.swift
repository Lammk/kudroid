import Network

/// Reports the system's default network path to the core, which the guest reads through
/// ConnectivityManager. Started once at launch so the first guest query already has an answer.
final class KuDroidNetworkMonitor {
    static let shared = KuDroidNetworkMonitor()

    private let monitor = NWPathMonitor()
    private let queue = DispatchQueue(label: "kudroid.network.path")
    private var started = false

    func start() {
        guard !started else { return }
        started = true
        monitor.pathUpdateHandler = { path in
            // ConnectivityManager.TYPE_*: a path over some other interface (VPN, loopback)
            // is reported as Wi-Fi, the transport a guest treats as a general connection.
            let transport: Int32
            if path.usesInterfaceType(.cellular) && !path.usesInterfaceType(.wifi) {
                transport = 0
            } else if path.usesInterfaceType(.wiredEthernet) && !path.usesInterfaceType(.wifi) {
                transport = 9
            } else {
                transport = 1
            }
            kudroid_set_network_state(path.status == .satisfied ? 1 : 0, transport,
                                      path.isExpensive ? 1 : 0, path.isConstrained ? 1 : 0)
        }
        monitor.start(queue: queue)
    }
}
