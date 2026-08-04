#version 330 core

uniform vec4 uColor;
uniform vec4 uRect;   // x, y, w, h in pixels
uniform float uRadius;   // corner radius in pixels (0 = sharp)
uniform float uSoft;     // edge softness in pixels (>=1 for AA)

in vec2 vLocal;
out vec4 FragColor;

// Signed distance to a rounded box centered at the origin.
float sdRoundBox(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + vec2(radius);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

void main() {
    float radius = clamp(uRadius, 0.0, min(uRect.z, uRect.w) * 0.5);
    if (radius <= 0.5) {
        FragColor = uColor;
        return;
    }

    vec2 halfSize = uRect.zw * 0.5;
    vec2 p = vLocal - halfSize;
    float d = sdRoundBox(p, halfSize, radius);

    // 1px-ish smooth edge. uSoft lets callers soften further for shadows.
    float soft = max(uSoft, 1.0);
    float alpha = 1.0 - smoothstep(-soft, soft, d);
    FragColor = vec4(uColor.rgb, uColor.a * alpha);
}
