#version 330 core

in vec2 vLocal;
in vec2 vFrag;

uniform vec4 uColor;      // rgb = dim colour (kept near black), a = overall strength
uniform float uTime;      // seconds, drives the slow scanline drift
uniform vec2  uViewport;

out vec4 FragColor;

// Fullscreen CRT scanline overlay, drawn ON TOP of the UI (but suppressed over
// video). It emits a dark tint whose alpha varies per scanline row so the rows
// between lines stay bright and the line rows dim — classic flowing scanlines.
// Pure ALU on one fullscreen quad — trivially cheap on a Pi 5.

void main() {
    // Flowing scanlines: a slow downward drift (uTime scrolls the pattern).
    // sin over pixel rows => alternating bright/dim lines, ~3px period.
    float phase = vFrag.y * 3.14159 * 0.66 - uTime * 1.4;
    float line = 0.5 + 0.5 * sin(phase);          // 0..1 per row
    float dim = 1.0 - line;                        // 1 on the dark band

    // Gentle large-scale roll bar (a faint bright band sweeping up the screen),
    // like a CRT refresh artefact. Very subtle so it never distracts.
    float roll = smoothstep(0.985, 1.0, sin(vFrag.y * 0.012 - uTime * 0.5) * 0.5 + 0.5);

    // Combine: scanline dimming is the main effect; roll adds a whisper of bloom.
    float alpha = uColor.a * (dim * 0.85 + roll * 0.15);

    FragColor = vec4(uColor.rgb, clamp(alpha, 0.0, 1.0));
}
