#version 450

#define LIGHT_POS vec4(-3., 3., 5., 1.)

struct Material
{
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
};

layout(location = 0) in vec3 viewPos;
layout(location = 1) in vec3 viewNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 0) out vec4 oColor;

layout(binding = 0) uniform Transforms
{
    mat4 normalMatrix;
    mat4 view;
    mat4 worldView;
    mat4 worldViewProj;
};

layout(binding = 1) uniform Materials
{
    Material light;
    Material surface;
};

layout(binding = 2) uniform sampler2D diffuse;

vec3 phong(vec3 N, vec3 L, vec3 V,
           vec3 Ka, vec3 Ia,
           vec3 Kd, vec3 Id,
           vec3 Ks, vec3 Is,
           float e)
{
    float NdL = max(dot(N, L), 0.);
    vec3 R = reflect(-L, N);
    float RdV = max(dot(R, V), 0.);
    return (Ka * Ia) + (Kd * NdL * Id) + (Ks * pow(RdV, e) * Is);
}

void main()
{
    vec3 diffuse = texture(diffuse, texCoord).rgb;
    vec3 lightViewPos = (view * LIGHT_POS).xyz;

    vec3 N = normalize(viewNormal);
    vec3 L = normalize(lightViewPos - viewPos);
    vec3 V = normalize(-viewPos); // view position at (0,0,0)

    vec3 color = phong(N, L, V,
        light.ambient, surface.ambient * diffuse,
        light.diffuse, surface.diffuse * diffuse,
        light.specular, surface.specular,
        surface.shininess);
    oColor = vec4(color, 1.);
}
