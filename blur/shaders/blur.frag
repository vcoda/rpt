#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 oColor;

layout(binding = 0) uniform sampler2D offscreen;

void main()
{
    oColor = texture(offscreen, texCoord);
}
