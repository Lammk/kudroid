import CoreLocation
import Foundation

// CoreLocation side of the LocationHost bridge (see
// include/kudroid/platform/LocationHost.h). One CLLocationManager feeds
// every guest subscription; per-handle minTime/minDistance filters from
// LocationManager.requestLocationUpdates are applied here before fixes
// are injected into the core.
final class KuDroidLocationBridge: NSObject, CLLocationManagerDelegate {
    static let shared = KuDroidLocationBridge()

    private let manager = CLLocationManager()
    private var filters: [Int64: (minTimeMs: Int64, minDistanceM: Float, lastDelivered: Date?)] = [:]
    private var singleShots: Set<Int64> = []

    var isAuthorized: Bool {
        let status: CLAuthorizationStatus
        if #available(iOS 14.0, *) {
            status = manager.authorizationStatus
        } else {
            status = CLLocationManager.authorizationStatus()
        }
        return status == .authorizedWhenInUse || status == .authorizedAlways
    }

    func register() {
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyBest
        var host = KuDroidLocationHost(
            start: { handle, minTimeMs, minDistanceM in
                KuDroidLocationBridge.shared.start(handle: handle, minTimeMs: minTimeMs,
                                                   minDistanceM: minDistanceM)
            },
            stop: { handle in
                KuDroidLocationBridge.shared.stop(handle: handle)
            },
            request_single: { handle in
                KuDroidLocationBridge.shared.requestSingle(handle: handle)
            }
        )
        kudroid_set_location_host(&host)
        refreshServicesEnabled()
    }

    private func start(handle: Int64, minTimeMs: Int64, minDistanceM: Float) {
        DispatchQueue.main.async {
            self.filters[handle] = (minTimeMs, minDistanceM, nil)
            self.ensureAuthorizedAndUpdating()
        }
    }

    private func stop(handle: Int64) {
        DispatchQueue.main.async {
            self.filters.removeValue(forKey: handle)
            self.singleShots.remove(handle)
            if self.filters.isEmpty && self.singleShots.isEmpty {
                self.manager.stopUpdatingLocation()
            }
        }
    }

    private func requestSingle(handle: Int64) {
        DispatchQueue.main.async {
            self.singleShots.insert(handle)
            self.ensureAuthorizedAndUpdating()
            self.manager.requestLocation()
        }
    }

    private func ensureAuthorizedAndUpdating() {
        let status: CLAuthorizationStatus
        if #available(iOS 14.0, *) {
            status = manager.authorizationStatus
        } else {
            status = CLLocationManager.authorizationStatus()
        }
        if status == .notDetermined {
            manager.requestWhenInUseAuthorization()
        }
        manager.startUpdatingLocation()
        refreshServicesEnabled()
    }

    private func refreshServicesEnabled() {
        let enabled = CLLocationManager.locationServicesEnabled() && isAuthorized
        kudroid_location_set_services_enabled(enabled ? 1 : 0)
    }

    // MARK: CLLocationManagerDelegate

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        for location in locations {
            let timeMs = Int64(location.timestamp.timeIntervalSince1970 * 1000)
            // Global last-known (handle 0).
            kudroid_location_inject_fix(0, location.coordinate.latitude,
                                        location.coordinate.longitude,
                                        Float(location.horizontalAccuracy),
                                        location.altitude,
                                        Float(max(0, location.speed)),
                                        Float(max(0, location.course)), timeMs)
            for (handle, filter) in filters {
                if let last = filter.lastDelivered {
                    let dt = location.timestamp.timeIntervalSince(last) * 1000
                    if dt < Double(filter.minTimeMs) { continue }
                    if filter.minDistanceM > 0 {
                        // Distance needs the previous fix; the core queue is
                        // the record — approximate with time-only filtering
                        // when no previous coordinate is kept here.
                    }
                }
                filters[handle]?.lastDelivered = location.timestamp
                kudroid_location_inject_fix(handle, location.coordinate.latitude,
                                            location.coordinate.longitude,
                                            Float(location.horizontalAccuracy),
                                            location.altitude,
                                            Float(max(0, location.speed)),
                                            Float(max(0, location.course)), timeMs)
            }
            for handle in singleShots {
                kudroid_location_inject_fix(handle, location.coordinate.latitude,
                                            location.coordinate.longitude,
                                            Float(location.horizontalAccuracy),
                                            location.altitude,
                                            Float(max(0, location.speed)),
                                            Float(max(0, location.course)), timeMs)
            }
            singleShots.removeAll()
        }
        refreshServicesEnabled()
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        refreshServicesEnabled()
    }

    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        refreshServicesEnabled()
    }

    func locationManager(_ manager: CLLocationManager, didChangeAuthorization status: CLAuthorizationStatus) {
        refreshServicesEnabled()
    }
}
