#version 300 es
precision highp float;
in vec2 vTexCoord;

uniform sampler2D uY8Texture;

out vec4 fragColor;

void main() {
    // Sample Y8 texture (only R channel has data)
    float y8 = texture(uY8Texture, vTexCoord).r;

    // Convert to YUV limited range (BT.709)
    // Y range: 16-235 (219 steps), so scale by 219/255 and offset by 16/255
    float y = y8 * (219.0 / 255.0) + (16.0 / 255.0);

    // UV set to neutral gray (0.5) - no color information
    float u = 0.5;
    float v = 0.5;

    // Output as RGBA (encoder will interpret as YUV420)
    fragColor = vec4(y, u, v, 1.0);
}
