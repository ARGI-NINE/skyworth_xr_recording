#version 300 es

in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec2 vTexcoord0;

uniform vec3 eyePos;
uniform vec3 modelColor;
uniform int useTexture;  // 0 = 使用 modelColor, 1 = 使用纹理(RGB), 2 = 使用纹理(灰度)

uniform sampler2D srcTex;

out highp vec4 outColor;



//--------------------------------------------------------------------------------------
void main()
//--------------------------------------------------------------------------------------
{
    if (useTexture == 1) {
        // **********************
        // Texture Value (sRGB from camera, convert to linear for sRGB swapchain output)
        // **********************
        vec4 srcColor = texture(srcTex, vTexcoord0);
        vec3 linear = srcColor.rgb * (srcColor.rgb * (srcColor.rgb * 0.305306011 + 0.682171111) + 0.012522878);
        outColor = vec4(linear, 1.0);
    } else if (useTexture == 2) {
        // **********************
        // Grayscale Texture (R8 format, display as grayscale)
        // **********************
        float gray = texture(srcTex, vTexcoord0).r;
        outColor = vec4(vec3(gray), 1.0);
    } else {
        // **********************
        // Solid Color
        // **********************
        outColor = vec4(modelColor, 1.0);
    }
}
