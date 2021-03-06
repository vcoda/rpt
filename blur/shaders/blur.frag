#version 450

#define M 7
#define N M

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 oColor;

layout(binding = 0) uniform sampler2D offscreen;

float gaussian(float sigma, int x)
{
    float d = float(x * x)/(2. * sigma * sigma);
    return exp(-d);
}

void main()
{
    const int m = M/2, n = N/2;
    const float sigma = 4.;

    vec2 texSize = 1./textureSize(offscreen, 0);

    for (int y = -n; y <= n; ++y)
    {
        float fy = gaussian(sigma, y);
        for (int x = -m; x <= m; ++x)
        {
            float fx = gaussian(sigma, x);
            float weight = fx * fy;
            vec2 duv = vec2(x, y) * texSize;

            oColor.rgb += texture(offscreen, texCoord + duv).rgb * weight;
            oColor.w += weight;
        }
    }

    oColor /= oColor.w;
}
