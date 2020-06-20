#version 450

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

layout(binding = 0) uniform Transforms
{
    mat4 normalMatrix;
    mat4 view;
    mat4 worldView;
    mat4 worldViewProj;
};

layout(location = 0) out vec3 oViewPos;
layout(location = 1) out vec3 oViewNormal;
layout(location = 2) out vec2 oTexCoord;
out gl_PerVertex {
    vec4 gl_Position;
};

void main()
{
    oViewPos = (worldView * position).xyz;
    oViewNormal = (normalMatrix * vec4(normal, 1.)).xyz;
    oTexCoord = texCoord;
    gl_Position = worldViewProj * position;
    gl_Position.y = -gl_Position.y;
}
