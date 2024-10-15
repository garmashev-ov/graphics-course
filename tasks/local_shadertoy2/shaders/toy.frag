#version 430
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D iChannel0;

layout(binding = 1) uniform sampler2D iChannel1;

layout(binding = 2) uniform sampler2D iChannel2;

layout(push_constant) uniform PushConstants
{
  float iTime;
  // vec2 iResolution;
  float iResolution_x;
  float iResolution_y;
  // vec2 iMouse;
  float iMouse_x;
  float iMouse_y;
}
params;

#define PI 3.14159

float rand(vec2 co)
{
  return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

float rand(float seed)
{
  return fract(sin(seed * 12.9898) * 43758.5453);
}

float smoothRand(float seed)
{
  return sin(2. * PI * fract(sin(seed * 1.9898) * 1.5453));
}

float smoothRand(vec2 seed)
{
  return sin(2. * PI * fract(sin(seed.x * 1.9898) * 1.5453 * seed.y));
}


vec3 random3(float seed)
{
  return normalize(vec3(
    rand(vec2(seed, seed + 1.)),
    rand(vec2(seed + 2., seed + 3.)),
    rand(vec2(seed + 4., seed + 5.))));
}

vec3 noise(float dist)
{
  return 10. * random3(params.iTime * 0.01 + dist) / (pow(dist + 1.13, 10.));
}

float sunspot(vec3 cDir)
{
  float spotsNum = 3.;
  float shadow = 0.;
  for (float i = 0.; i < spotsNum; ++i)
  {
    vec3 spot = vec3(
                  smoothRand(4200. * i + params.iTime * 0.1),
                  smoothRand(4300. * i + params.iTime * 0.1),
                  smoothRand(4400. * i + params.iTime * 0.1)) *
      0.1;
    shadow += .3 / pow((length(spot - cDir) + 1.), 5.);
  }

  vec3 spot = vec3(
                smoothRand(42. + params.iTime * 0.2),
                smoothRand(43. + params.iTime * 0.2),
                smoothRand(44. + params.iTime * 0.2)) *
    0.1;
  return min(
    shadow + abs(smoothRand(params.iTime * 0.5 + 12.5 * (fract(cDir.y * 10.)))) * 0.05, 0.8);
}


void mainCubemap(out vec3 fragColor, in vec3 rayDir)
{
  // Ray direction as color
  vec3 center = normalize(vec3(2, 3, 10));
  vec3 cDir = (center - rayDir);
  vec3 col = 0.1 + 0.5 * (rayDir) + 0.5 * vec3(2, 0.5, 0.5) / (length(cDir) + 1.) +
    7. * random3(5. * params.iTime + length(cDir)) / (pow(length(cDir) + 1.13, 10.));
  col.g *= 0.7;
  if (length(cDir) < 0.2)
  {
    col = vec3(1., 3.5 * length(center - rayDir), 0.);
    float spotIntensity = sunspot(cDir);
    col *= (1.0 - spotIntensity);
  }
  else if (length(cDir) < 0.3 && mod((cDir.x / cDir.y), 0.1) > 0.02)
  {
    col *= 0.1 / length(cDir);
    col += (1. - (0.1 / length(cDir))) *
      (vec3(1., 3.5 * length(center - rayDir), 0.) - 0.7 * noise(length(cDir)));
  }

  // Output to cubemap
  fragColor = col;
}


struct Surface
{
  float sd;
  vec3 color;
};

mat3 rotateX(float theta)
{
  float c = cos(theta);
  float s = sin(theta);
  return mat3(vec3(1, 0, 0), vec3(0, c, -s), vec3(0, s, c));
}

// Rotation matrix around the Y axis.
mat3 rotateY(float theta)
{
  float c = cos(theta);
  float s = sin(theta);
  return mat3(vec3(c, 0, s), vec3(0, 1, 0), vec3(-s, 0, c));
}

// Rotation matrix around the Z axis.
mat3 rotateZ(float theta)
{
  float c = cos(theta);
  float s = sin(theta);
  return mat3(vec3(c, -s, 0), vec3(s, c, 0), vec3(0, 0, 1));
}

Surface sdfScene(in vec3 p);

vec3 generateNormal(vec3 z)
{
  float e = 0.001;
  float dx1 = sdfScene(z + vec3(e, 0, 0)).sd;
  float dx2 = sdfScene(z - vec3(e, 0, 0)).sd;
  float dy1 = sdfScene(z + vec3(0, e, 0)).sd;
  float dy2 = sdfScene(z - vec3(0, e, 0)).sd;
  float dz1 = sdfScene(z + vec3(0, 0, e)).sd;
  float dz2 = sdfScene(z - vec3(0, 0, e)).sd;

  return -normalize(vec3(dx1 - dx2, dy1 - dy2, dz1 - dz2));
}

vec3 getTexture(vec3 p, vec3 normal, sampler2D channel)
{
  p = mod(p, 1);
  return (
    abs(texture(channel, p.xy).rgb * normal.z) +
    abs(texture(channel, p.xz).rgb * normal.y) +
    abs(texture(channel, p.yz).rgb * normal.x));
}

Surface minSD(in Surface s1, in Surface s2)
{
  if (s1.sd < s2.sd)
  {
    return s1;
  }
  return s2;
}

float sphereSD(vec3 p)
{
  return length(p) - 1.;
}

vec3 sphereNormal(vec3 z)
{
  float e = 0.001;
  float dx1 = sphereSD(z + vec3(e, 0, 0));
  float dx2 = sphereSD(z - vec3(e, 0, 0));
  float dy1 = sphereSD(z + vec3(0, e, 0));
  float dy2 = sphereSD(z - vec3(0, e, 0));
  float dz1 = sphereSD(z + vec3(0, 0, e));
  float dz2 = sphereSD(z - vec3(0, 0, e));

  return -normalize(vec3(dx1 - dx2, dy1 - dy2, dz1 - dz2));
}

Surface sphere(in vec3 p, in vec3 offset)
{
  p -= offset;
  p *= rotateX(params.iTime) * rotateY(params.iTime) * rotateZ(params.iTime);
  vec3 normal = sphereNormal(p);
  return Surface(sphereSD(p), getTexture(p, normal, iChannel0));
}

float torusSD(vec3 pt, vec2 t)
{
  vec2 q = vec2(length(pt.xz) - t.x, pt.y);
  return length(q) - t.y;
}

vec3 torusNormal(vec3 z, vec2 t)
{
  float e = 0.001;
  float dx1 = torusSD(z + vec3(e, 0, 0), t);
  float dx2 = torusSD(z - vec3(e, 0, 0), t);
  float dy1 = torusSD(z + vec3(0, e, 0), t);
  float dy2 = torusSD(z - vec3(0, e, 0), t);
  float dz1 = torusSD(z + vec3(0, 0, e), t);
  float dz2 = torusSD(z - vec3(0, 0, e), t);

  return -normalize(vec3(dx1 - dx2, dy1 - dy2, dz1 - dz2));
}

Surface torus(in vec3 pos, in vec2 t, in vec3 offset)
{
  vec3 pt = (pos - offset) * rotateX(params.iTime * 0.3) * rotateY(params.iTime * 0.2) *
    rotateX(params.iTime * 0.5);
  vec3 normal = torusNormal(pt, t);
  return Surface(torusSD(pt, t), getTexture(pt, normal, iChannel0));
}

float cubeSD(vec3 p)
{
  vec3 pt = abs(p) - 1.0;
  return length(max(pt, 0.0)) + min(max(pt.x, max(pt.y, pt.z)), 0.0);
}

vec3 cubeNormal(vec3 z)
{
  float e = 0.001;
  float dx1 = cubeSD(z + vec3(e, 0, 0));
  float dx2 = cubeSD(z - vec3(e, 0, 0));
  float dy1 = cubeSD(z + vec3(0, e, 0));
  float dy2 = cubeSD(z - vec3(0, e, 0));
  float dz1 = cubeSD(z + vec3(0, 0, e));
  float dz2 = cubeSD(z - vec3(0, 0, e));

  return -normalize(vec3(dx1 - dx2, dy1 - dy2, dz1 - dz2));
}

Surface cube(in vec3 pos, in vec3 offset)
{
  vec3 pt = (pos - offset) * rotateX(params.iTime) * rotateY(params.iTime) *
    rotateZ(params.iTime);
  ;
  vec3 col = getTexture(pt, cubeNormal(pt), iChannel2);
  Surface cube1 = Surface(cubeSD(pt), col);

  vec3 pt2 = (pos - offset) * rotateX(params.iTime + 1.) * rotateY(params.iTime + 1.) *
    rotateZ(params.iTime);
  vec3 col2 = getTexture(pt2, cubeNormal(pt2), iChannel2);
  Surface cube2 = Surface(cubeSD(pt2), col2);

  return minSD(cube1, cube2);
}

Surface sdfFloor(vec3 p)
{
  // return Surface(p.y + 10., texture(iChannel0, p.xz * 0.0625).rgb);
  return Surface(p.y + 10., texture(iChannel1, mod(p.xz * 0.04, 1)).rgb);
}


Surface sdfScene(in vec3 p)
{
  return minSD(
    minSD(
      minSD(torus(p, vec2(1, 0.5), vec3(-3, 0, -4)), sphere(p, vec3(4, 1, -3))),
      sdfFloor(p)),
    cube(p, vec3(1, 0, -2)));
}


#define MAX_ITERS 300
#define MAX_DIST 200.0
vec3 trace(vec3 from, vec3 dir, out bool hit, out int steps)
{
  vec3 p = from;
  float totalDist = 0.0;

  hit = false;

  for (steps = 0; steps < MAX_ITERS; steps++)
  {
    Surface dist = sdfScene(p);

    if (dist.sd < 0.00001)
    {
      hit = true;
      break;
    }

    totalDist += dist.sd;

    if (totalDist > MAX_DIST)
      break;

    p += dist.sd * dir;
  }

  return p;
}

mat3 camera(vec3 cameraPos, vec3 lookAtPoint)
{
  vec3 cd = normalize(lookAtPoint - cameraPos);  // camera direction
  vec3 cr = normalize(cross(vec3(0, 1, 0), cd)); // camera right
  vec3 cu = normalize(cross(cd, cr));            // camera up

  return mat3(-cr, cu, -cd);
}

mat2 rotate2d(float theta)
{
  float s = sin(theta), c = cos(theta);
  return mat2(c, -s, s, c);
}

vec3 powVec3(vec3 vec, float num)
{
  vec.x = pow(vec.x, num);
  vec.y = pow(vec.y, num);
  vec.z = pow(vec.z, num);
  return vec;
}


#define PI 3.14159


void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
  vec2 iResolution = vec2(params.iResolution_x, params.iResolution_y);
  vec2 uv = (fragCoord - .5 * iResolution.xy) / iResolution.y;
  uv.y = -uv.y;
  vec2 iMouse = vec2(params.iMouse_x, params.iMouse_y);
  vec2 mouseUV = iMouse.xy / iResolution.xy;
  if (mouseUV == vec2(0, 0))
  {
    mouseUV = vec2(0.5, 0.5);
  }
  vec3 col = vec3(0.1, 0.1, 0.1);
  vec3 lp = vec3(0, 0, 0); // lookat point (aka camera target)
  vec3 ro = vec3(0, 3, 3); // ray origin that represents camera position
  float cameraRadius = 2.5;
  ro.yz = ro.yz * cameraRadius * rotate2d(mix(PI / 2., 0., mouseUV.y));
  ro.xz = ro.xz * rotate2d(mix(-PI, PI, mouseUV.x)) + vec2(lp.x, lp.z);
  vec3 rd = camera(ro, lp) * normalize(vec3(uv, -1)); // ray direction
  vec3 light = vec3(2, 3, 10);
  bool hit;
  int steps;
  vec3 p = trace(ro, rd, hit, steps);
  vec3 light_dir = normalize(p - light);
  vec3 normal = generateNormal(p);

  if (hit)
  {
    vec3 specular =
      pow(max(0., dot(normalize(light_dir + normalize(rd)), normal)), 500.) *
      vec3(1, 1, 1);
    col = max(0.25, dot(normal, light_dir)) * sdfScene(p).color + specular;
    vec3 light_p = trace(light, light_dir, hit, steps);
    if (length(light_p - p) > 0.1)
    {
      col *= 0.5;
    }
  }
  else
  {
    mainCubemap(col, rd);
  }
  fragColor = vec4(powVec3(col, 0.7), 1);
}


void main()
{
  mainImage(color, gl_FragCoord.xy);
}
