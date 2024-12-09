#version 430

layout(push_constant) uniform params_t
{
  uvec2 iResolution;
  vec2 padding;
}
params;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 fragCoord = gl_FragCoord.xy;
    fragCoord.x *= (float(params.iResolution.y) / params.iResolution.x);
    float step = 60.;

    vec3 col = vec3(0);
    if (mod(abs(fragCoord.x) / step , 2.) <= 1. && mod(abs(fragCoord.y) / step , 2.) <= 1.) {
        col.r += 1.;
    } else if (mod(abs(fragCoord.x) / step , 2.) <= 1. && mod(abs(fragCoord.y) / step , 2.) > 1.) {
        col.g += 1.;
    } else if (mod(abs(fragCoord.x) / step , 2.) > 1. && mod(abs(fragCoord.y) / step , 2.) > 1.) {
        col.b += 1.;
    } else {
        col += 1.;
    }
    fragColor = vec4(col, 1.0);
}