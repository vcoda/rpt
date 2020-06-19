#version 450

#define BG_COLOR vec4(1.)
#define FILL_COLOR vec4(1., 0.4313, 0.3411, 1.)
#define SIZE vec2(32.)

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 oColor;

void main()
{
    vec2 checker = mod(gl_FragCoord.xy, SIZE * 2.);
    bool fill;
    if (checker.x < SIZE.x)
        fill = (checker.y > SIZE.y);
    else
        fill = (checker.y < SIZE.y);
    oColor = fill ? FILL_COLOR : BG_COLOR;
}
