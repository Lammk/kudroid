import SwiftUI
import UIKit

// Library buttons receive touches immediately while scrolling can still cancel them.
struct ImmediateScrollTouches: UIViewRepresentable {
    final class Probe: UIView {
        override func didMoveToSuperview() {
            super.didMoveToSuperview()
            updateScrollView()
        }

        override func didMoveToWindow() {
            super.didMoveToWindow()
            updateScrollView()
        }

        private func updateScrollView() {
            var ancestor = superview
            while let view = ancestor {
                if let scroll = view as? UIScrollView {
                    scroll.delaysContentTouches = false
                    return
                }
                ancestor = view.superview
            }
        }
    }

    func makeUIView(context: Context) -> Probe {
        let view = Probe()
        view.isUserInteractionEnabled = false
        return view
    }

    func updateUIView(_ uiView: Probe, context: Context) {}
}
