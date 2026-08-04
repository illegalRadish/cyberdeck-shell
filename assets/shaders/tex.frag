#version 330 core

in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
out vec4 FragColor;

void main() {
    FragColor = texture(uTex, vUV) * uColor;
}
