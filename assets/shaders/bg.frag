#version 330 core

in vec2 vLocal;
in vec2 vFrag;

uniform vec4 uColor;      // tint applied to the CRT glow
uniform float uTime;      // seconds, for a slow scanline drift
uniform vec2  uViewport;

out vec4 FragColor;

// Very cheap CRT / phosphor backdrop:
//  - faint horizontal scanlines
//  - subtle vertical grille
//  - soft vignette toward the edges
//  - a slow, gentle brightness "breath"
// All pure ALU on a single fullscreen quad — trivially cheap on a Pi 5.

void main() {
    // Scanlines: darken alternate pixel rows slightly.
    float scan = 0.92 + 0.08 * sin(vFrag.y * 3.14159 + uTime * 0.6);

    // Vertical grille (very subtle).
    float grille = 0.96 + 0.04 * sin(vFrag.x * 3.14159);

    // Vignette: radial falloff from centre.
    vec2 uv = vLocal * 2.0 - 1.0;
    float vig = 1.0 - dot(uv, uv) * 0.28;
    vig = clamp(vig, 0.0, 1.0);

    // Slow breath so the phosphor feels alive.
    float breath = 0.97 + 0.03 * sin(uTime * 0.8);

    float intensity = scan * grille * vig * breath;
    vec3 rgb = uColor.rgb * intensity;
    FragColor = vec4(rgb, uColor.a);
}
