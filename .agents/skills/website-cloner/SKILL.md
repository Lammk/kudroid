---
name: website-cloner
description: Reverse-engineer and clone any target website into a high-quality, modern web application (Next.js / React / HTML+CSS). Combines AI Website Cloner multi-phase pipeline (Recon -> Spec -> Build -> Validate) with Superdesign DNA extraction.
metadata:
  author: Antigravity AI
  version: 1.0.0
---

# AI Website Cloner & Reverse-Engineering Skill

This skill provides a complete workflow for analyzing, extracting, and cloning any live website into production-ready code, integrating both **Superdesign Engine** and **Multi-Phase Agentic Cloning Pipeline**.

---

## 🎯 Dual Engine Architecture

When asked to clone, recreate, or extract a website, choose the appropriate mode or combine both:

### Mode 1: Superdesign DNA Extraction & Interactive Canvas (`superdesign`)
Best for extracting design tokens, colors, typography, assets, and generating editable UI draft variants on the canvas.
```bash
npx --yes @superdesign/cli@latest extract-website --url <target-url> --design-md
```

### Mode 2: Multi-Phase Deep Website Reconstruction (`website-cloner`)
Best for full-page structural cloning, responsive behavior replication, component spec writing, and building a complete Next.js / React / Vanilla CSS app.

---

## 🚀 5-Phase Cloning Pipeline

### Phase 1: Reconnaissance & Asset Extraction
1. **URL Inspection**: Fetch page content, computed CSS, fonts, and images using web reading / browser tools.
2. **Design Tokens**: Extract color palette (primary, secondary, background, muted), font families, grid spacing, line heights, border radii, and shadows.
3. **Asset Inventory**: Download/save logos, icons, hero images, and background graphics.

### Phase 2: Component Specification
Break down the target site into distinct, modular component specs:
- `Navigation / Header`: Brand logo, navigation links, CTA buttons, mobile drawer.
- `Hero Section`: Headline typography, subtext, key visual, CTA group.
- `Feature Bento / Grid`: Cards, icons, subtle hover effects, micro-animations.
- `Content / Showcase Sections`: Testimonials, stats, pricing tables, interactive elements.
- `Footer`: Links, copyright, social icons, newsletter form.

### Phase 3: Project Setup & Foundation
- Setup modern layout using target stack (Next.js 15+ / Vite / Vanilla HTML+CSS).
- Define global CSS variables (`globals.css` / HSL tokens) matching extracted tokens.
- Apply modern typography (Google Fonts like Inter, Outfit, Plus Jakarta Sans).

### Phase 4: Component Implementation
- Build components incrementally from foundation upward (primitives -> sections -> page assembly).
- Ensure strict anti-slop guidelines: no cheap default gradients, subtle transitions, clean spacing.
- Maintain responsive layouts across mobile, tablet, and desktop breakpoints.

### Phase 5: Visual QA & Refinement
- Verify rendered UI against original design tokens.
- Ensure all micro-interactions, hover states, and smooth transitions feel fluid.
- Remove filler code, optimize image assets, and format cleanly.

---

## 💡 Quick Trigger Commands

- **Full Clone**: `"Clone website <url> into this project"`
- **Style Extraction Only**: `"Extract design tokens and style guide from <url>"`
- **Superdesign Canvas Import**: `"Extract <url> to Superdesign design-system.md"`
