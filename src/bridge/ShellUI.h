// ShellUI.h — seam for shell-facing UI plumbing (Metal layer, software canvas,
// haptics, keep-screen-on, soft keyboard).
//
// ShellUI.cpp defines the g_metalLayer globals that BionicShim and
// JavaCanvasRenderer read; the declarations here are the single source of
// truth for the other bridge units.

#pragma once

// Metal layer + cached metrics, set by the Swift shell through
// kudroid_set_metal_layer and read by the GPU shims.
extern void* g_metalLayer;
extern int g_metalLayerWidth;
extern int g_metalLayerHeight;
extern float g_metalLayerDensity;

// Framebuffer dimensions for the software canvas fallback. 0 means "not
// bound"; ShellUI.cpp falls back to 1080x1920 in that case.
void shellUiEnsureSoftwareFramebuffer();
