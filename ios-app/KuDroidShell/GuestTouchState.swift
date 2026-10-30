import Foundation
#if canImport(UIKit)
import UIKit
#endif

// Keep every pointer alive until its release has reached the guest.
final class TouchPointerTracker {
    typealias Pointer = (id: Int32, x: Float, y: Float)
    typealias Batch = (action: Int32, primary: Int32, pointers: [Pointer])

    private var ids: [ObjectIdentifier: Int32] = [:]
    // Retain tracked touches so a missing contact cannot share a reused object address.
    private var contacts: [ObjectIdentifier: UITouch] = [:]
    private var order: [ObjectIdentifier] = []
    private var positions: [ObjectIdentifier: (x: Float, y: Float)] = [:]
    private var reconciliations = 0

    var isEmpty: Bool { ids.isEmpty }

    func begin(_ touch: UITouch) -> Int32? {
        let key = ObjectIdentifier(touch)
        if let id = ids[key] { return id }
        let used = Set(ids.values)
        guard let assigned = (Int32(0)..<32).first(where: { !used.contains($0) }) else { return nil }
        ids[key] = assigned
        contacts[key] = touch
        order.append(key)
        return assigned
    }

    private func end(_ key: ObjectIdentifier) {
        order.removeAll { $0 == key }
        ids.removeValue(forKey: key)
        contacts.removeValue(forKey: key)
        positions.removeValue(forKey: key)
    }

    func activePointers(live: Set<UITouch>, in view: UIView, scale: CGFloat) -> [Pointer] {
        for touch in live {
            let key = ObjectIdentifier(touch)
            guard ids[key] != nil else { continue }
            let location = touch.location(in: view)
            positions[key] = (Float(location.x * scale), Float(location.y * scale))
        }
        return order.compactMap { key in
            guard let id = ids[key], let position = positions[key] else { return nil }
            return (id, position.x, position.y)
        }
    }

    func cancel() -> Batch? {
        let pointers: [Pointer] = order.compactMap { key in
            guard let id = ids[key], let position = positions[key] else { return nil }
            return (id, position.x, position.y)
        }
        ids.removeAll()
        contacts.removeAll()
        order.removeAll()
        positions.removeAll()
        guard let first = pointers.first else { return nil }
        return (3, first.id, pointers)
    }

    // Missing contacts must leave through UP/CANCEL before their ids can be reused.
    func reconcile(live: Set<UITouch>, current: Set<UITouch>, in view: UIView,
                   scale: CGFloat) -> [Batch] {
        _ = activePointers(live: live.union(current), in: view, scale: scale)
        var keep = Set(current.map { ObjectIdentifier($0) })
        for touch in live where touch.phase != .ended && touch.phase != .cancelled {
            keep.insert(ObjectIdentifier(touch))
        }
        let missing = order.filter { !keep.contains($0) }
        guard !missing.isEmpty else { return [] }
        if reconciliations < 20 {
            reconciliations += 1
            NSLog("[KuDroidTouch] releasing missing contacts=%d tracked=%d", missing.count, ids.count)
        }
        if missing.count == ids.count { return cancel().map { [$0] } ?? [] }
        var batches: [Batch] = []
        for key in missing {
            if let id = ids[key] {
                let pointers = activePointers(live: live, in: view, scale: scale)
                batches.append((pointers.count > 1 ? 6 : 1, id, pointers))
            }
            end(key)
        }
        return batches
    }

    func release(_ touches: Set<UITouch>, live: Set<UITouch>, in view: UIView,
                 scale: CGFloat, cancelled: Bool) -> [Batch] {
        var batches = reconcile(live: live, current: touches, in: view, scale: scale)
        let keys = Set(touches.map { ObjectIdentifier($0) })
        let leaving = order.filter { keys.contains($0) }
        guard !leaving.isEmpty else { return batches }
        if cancelled && leaving.count == ids.count {
            if let batch = cancel() { batches.append(batch) }
            return batches
        }
        for key in leaving {
            if let id = ids[key] {
                let pointers = activePointers(live: live.union(touches), in: view, scale: scale)
                batches.append((pointers.count > 1 ? 6 : 1, id, pointers))
            }
            end(key)
        }
        return batches
    }
}

