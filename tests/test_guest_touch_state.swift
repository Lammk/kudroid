import Foundation

// UIKit contact identities and terminal phases for the shared pointer tracker.
final class UIView {}

final class UITouch: Hashable {
    enum Phase { case began, moved, stationary, ended, cancelled }
    var phase: Phase
    var position: CGPoint

    init(_ x: CGFloat, _ y: CGFloat, phase: Phase = .began) {
        self.position = CGPoint(x: x, y: y)
        self.phase = phase
    }

    func location(in view: UIView) -> CGPoint { position }
    static func == (lhs: UITouch, rhs: UITouch) -> Bool { lhs === rhs }
    func hash(into hasher: inout Hasher) { hasher.combine(ObjectIdentifier(self)) }
}

@main
struct GuestTouchStateTests {
    static var checks = 0
    static var failures = 0

    static func check(_ condition: Bool, _ description: String) {
        checks += 1
        if !condition { failures += 1 }
        print("\(condition ? "OK" : "FAIL") \(description)")
    }

    static func main() {
        let view = UIView()
        let state = TouchPointerTracker()
        let held = UITouch(12, 34)
        check(state.begin(held) == 0, "first contact gets pointer zero")
        _ = state.activePointers(live: [held], in: view, scale: 2)
        held.phase = .stationary

        for index in 0..<8 {
            let tapped = UITouch(CGFloat(50 + index), 80)
            let premature = state.reconcile(live: [held, tapped], current: [tapped], in: view, scale: 2)
            check(premature.isEmpty, "stationary contact survives second-finger begin \(index)")
            check(state.begin(tapped) == 1, "second finger retains the available pointer ID \(index)")
            let pointers = state.activePointers(live: [held, tapped], in: view, scale: 2)
            check(pointers.count == 2 && pointers.first?.id == 0
                  && pointers.first?.x == 24 && pointers.first?.y == 68,
                  "stationary contact retains ID, order and coordinates \(index)")
            tapped.phase = .ended
            let release = state.release([tapped], live: [held, tapped], in: view, scale: 2, cancelled: false)
            check(release.count == 1 && release.first?.action == 6
                  && release.first?.primary == 1 && release.first?.pointers.count == 2,
                  "second-finger lift reports POINTER_UP with both contacts \(index)")
        }

        held.phase = .ended
        let ended = state.reconcile(live: [], current: [], in: view, scale: 2)
        check(ended.count == 1 && ended.first?.action == 3 && ended.first?.primary == 0,
              "a missing final contact cancels the tracked gesture")
        check(state.isEmpty, "terminal release frees the tracked contact")

        let first = UITouch(1, 2)
        let second = UITouch(3, 4)
        check(state.begin(first) == 0 && state.begin(second) == 1, "released IDs can be reused")
        _ = state.activePointers(live: [first, second], in: view, scale: 1)
        let missingActive = state.reconcile(live: [], current: [], in: view, scale: 1)
        check(missingActive.count == 1 && missingActive.first?.action == 3 && state.isEmpty,
              "empty event membership cancels the tracked gesture")
        _ = state.begin(first)
        _ = state.begin(second)
        _ = state.activePointers(live: [first, second], in: view, scale: 1)
        first.phase = .cancelled
        second.phase = .cancelled
        let cancelled = state.reconcile(live: [], current: [], in: view, scale: 1)
        check(cancelled.count == 1 && cancelled.first?.action == 3
              && cancelled.first?.pointers.count == 2 && state.isEmpty,
              "missing cancelled contacts emit one complete CANCEL")

        let left = UITouch(10, 20)
        let right = UITouch(30, 40)
        _ = state.begin(left)
        _ = state.begin(right)
        _ = state.activePointers(live: [left, right], in: view, scale: 1)
        left.phase = .ended
        right.phase = .ended
        let both = state.release([left, right], live: [left, right], in: view, scale: 1, cancelled: false)
        check(both.count == 2 && both[0].action == 6 && both[0].primary == 0
              && both[0].pointers.map(\.id) == [0, 1] && both[1].action == 1
              && both[1].primary == 1 && both[1].pointers.map(\.id) == [1],
              "simultaneous releases preserve pointer ordering through final UP")

        let keep = UITouch(5, 6)
        let lost = UITouch(7, 8)
        _ = state.begin(keep)
        _ = state.begin(lost)
        _ = state.activePointers(live: [keep, lost], in: view, scale: 1)
        lost.phase = .ended
        lost.position = CGPoint(x: 9, y: 10)
        let recovered = state.reconcile(live: [keep, lost], current: [keep], in: view, scale: 1)
        check(recovered.count == 1 && recovered.first?.action == 6
              && recovered.first?.primary == 1 && recovered.first?.pointers.count == 2
              && recovered.first?.pointers.last?.x == 9 && recovered.first?.pointers.last?.y == 10,
              "an ended contact leaves without releasing the active finger")
        check(state.cancel()?.pointers.map(\.id) == [0] && state.isEmpty,
              "view cancellation releases the remaining contact exactly once")
        check(state.cancel() == nil, "repeated view cancellation emits no phantom gesture")

        let wide = (0..<33).map { UITouch(CGFloat($0), 1) }
        for index in 0..<32 { _ = state.begin(wide[index]) }
        check(state.begin(wide[32]) == nil, "pointer capacity cannot duplicate an active ID")
        _ = state.activePointers(live: Set(wide), in: view, scale: 1)
        check(state.cancel()?.pointers.count == 32, "capacity cancellation includes every tracked pointer")
        print("\(checks) checks, \(failures) failures")
        if failures != 0 { exit(1) }
    }
}
