#version 300 es
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 3) in vec2 texcoord0;

uniform mat4 projectionMatrix;
uniform mat4 viewMatrix;
uniform mat4 modelMatrix;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vTexcoord0;

void main()
{
    gl_Position = projectionMatrix * (viewMatrix * (modelMatrix * vec4(position, 1.0)));
    vWorldPos = (modelMatrix * vec4(position.xyz, 1.0)).xyz;
    // Only rotate the rest of these!
    vWorldNormal = (modelMatrix * vec4(normal.xyz, 0.0)).xyz;

    vTexcoord0 = vec2(texcoord0.x, 1.0 - texcoord0.y);
}
