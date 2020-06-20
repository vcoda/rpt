#version 450

#define LIGHT_POS vec4(-3., 3., 5., 1.)

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
layout(binding = 1) uniform sampler2D diffuse;

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
    vec3 lightAmbient = vec3(0.25);
    vec3 lightDiffuse = vec3(1.0, 0.9, 0.8);
    vec3 lightSpecular = vec3(1.0);

    vec3 matDiffuse = texture(diffuse, texCoord).rgb;
    vec3 matSpecular = matDiffuse;
    float matAmbient = 0.25;
    float matShininess = 4.; // Metallic

    vec3 lightViewPos = (view * LIGHT_POS).xyz;

    vec3 N = normalize(viewNormal);
    vec3 L = normalize(lightViewPos - viewPos);
    vec3 V = normalize(-viewPos); // view position at (0,0,0)

    vec3 color = phong(N, L, V,
        lightAmbient, matDiffuse * matAmbient,
        lightDiffuse, matDiffuse.rgb,
        lightSpecular, matSpecular,
        matShininess);
    oColor = vec4(color, 1.);
}
