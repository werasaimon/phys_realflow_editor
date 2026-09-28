#pragma once
// The GLSL sources of the viewport's programs, GLSL 1.30 (OpenGL 3.0, also Mesa's llvmpipe) with
// the version header prepended by Viewport::initializeGL (buildProgram), 3.30 core where available.

// Colormaps (turbo, cool-warm, gray, viridis) shared by the fragment shaders that colour by a field.
static const char* kColormapGLSL = R"(
vec3 turbo(float x) {
    const vec4 kR4 = vec4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    const vec4 kG4 = vec4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
    const vec4 kB4 = vec4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    const vec2 kR2 = vec2(-152.94239396, 59.28637943);
    const vec2 kG2 = vec2(4.27729857, 2.82956604);
    const vec2 kB2 = vec2(-89.90310912, 27.34824973);
    x = clamp(x, 0.0, 1.0);
    vec4 v4 = vec4(1.0, x, x * x, x * x * x);
    vec2 v2 = v4.zw * v4.z;
    return vec3(dot(v4, kR4) + dot(v2, kR2), dot(v4, kG4) + dot(v2, kG2), dot(v4, kB4) + dot(v2, kB2));
}
vec3 viridis(float t) {
    t = clamp(t, 0.0, 1.0);
    const vec3 c0 = vec3(0.2777273272234177, 0.005407344544966578, 0.3340998053353061);
    const vec3 c1 = vec3(0.1050930431085774, 1.404613529898575, 1.384590162594685);
    const vec3 c2 = vec3(-0.3308618287255563, 0.214847559468213, 0.09509516302823659);
    const vec3 c3 = vec3(-4.634230498983486, -5.799100973351585, -19.33244095627987);
    const vec3 c4 = vec3(6.228269936347081, 14.17993336680509, 56.69055260068105);
    const vec3 c5 = vec3(4.776384997670288, -13.74514537774601, -65.35303263337234);
    const vec3 c6 = vec3(-5.435455855934631, 4.645852612178535, 26.3124352495832);
    return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}
vec3 coolwarm(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 a = vec3(0.230, 0.299, 0.754), m = vec3(0.865, 0.865, 0.865), b = vec3(0.706, 0.016, 0.150);
    return t < 0.5 ? mix(a, m, t * 2.0) : mix(m, b, t * 2.0 - 1.0);
}
vec3 cmap(int m, float t) {
    if (m == 1) return viridis(t);
    if (m == 2) return coolwarm(t);
    if (m == 3) return vec3(clamp(t, 0.0, 1.0));
    return turbo(t);
}
)";

// Background: a full-screen gradient.
static const char* kBgVS = R"(out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.9999, 1.0);
}
)";
static const char* kBgFS = R"(in vec2 uv; out vec4 o;
void main() {
    vec3 top = vec3(0.17, 0.19, 0.23), bot = vec3(0.06, 0.07, 0.09);
    o = vec4(mix(bot, top, uv.y), 1.0);
}
)";

// Selection outlines: the selected (red channel), hovered (green) and role-less (blue) objects are
// drawn flat into a mask; this pass lights the pixels just outside each silhouette - within uRadius
// pixels of the mask but not in it - so every shape gets its own contour, as in Blender or Maya.
static const char* kOutlineFS = R"(in vec2 uv; out vec4 o;
uniform sampler2D uMask;
uniform vec2 uTexel;
uniform float uRadius;
uniform vec4 uSelected, uHover, uGhost;
void main() {
    vec3 inside = texture(uMask, uv).rgb;
    vec3 near = inside;
    for (int i = 0; i < 12; ++i) {
        float a = 6.2831853 * float(i) / 12.0;
        vec2 d = vec2(cos(a), sin(a)) * uTexel;
        near = max(near, texture(uMask, uv + d * uRadius).rgb);
        near = max(near, texture(uMask, uv + d * (0.5 * uRadius)).rgb);
    }
    vec3 edge = clamp(near - inside, 0.0, 1.0);
    vec4 c = vec4(0.0);
    if (edge.b > 0.0) c = vec4(uGhost.rgb, uGhost.a * edge.b);
    if (edge.g > 0.0) c = vec4(uHover.rgb, uHover.a * edge.g);
    if (edge.r > 0.0) c = vec4(uSelected.rgb, uSelected.a * edge.r);
    if (c.a <= 0.003) discard;
    o = c;
}
)";

// Lit meshes: the obstacle, rigid bodies, cloth; also the glass vessel (uAlpha < 1).
// Lighting: the scene's own lights when it has any (RenderSnapshot::lights: up to eight suns, lamps
// and spotlights, all in view space) - Lambert diffuse, a soft Blinn-Phong highlight, a lamp fading
// quadratically to nothing at its range, a spotlight's cone with a smooth edge - over a constant
// ambient. Up to four of them cast shadows (ViewportShadows.cpp): a sun or a spotlight from a depth
// map, a lamp from a cube of distances, each softened by a few neighbouring samples (PCF) - compiled
// in only while some light casts one (RF_SHADOWS). No lights: the viewer's own light from the top
// left and a head light, as before.
static const char* kMeshVS = R"(in vec3 aPos;
in vec3 aNormal;
in float aScalar;
uniform mat4 uModel, uView, uProj;
out vec3 vN; out vec3 vPosV; out float vS;
void main() {
    vec4 vp = uView * uModel * vec4(aPos, 1.0);
    vPosV = vp.xyz;
    vN = mat3(uView) * mat3(uModel) * aNormal;
    vS = aScalar;
    gl_Position = uProj * vp;
}
)";
static const char* kMeshFS = R"(
in vec3 vN; in vec3 vPosV; in float vS;
uniform vec3 uColor; uniform int uUseScalar; uniform float uMin, uMax; uniform int uCmap;
uniform float uAlpha = 1.0; // < 1: glass (drawn blended, after the volume)
uniform int uLightCount;     // the scene's lights, up to 8: suns, lamps and spotlights
uniform vec3 uLightPos[8], uLightDir[8], uLightColor[8]; // uLightDir: where it shines; colour * intensity
uniform float uLightRange[8], uLightCosOuter[8], uLightCosInner[8]; // a lamp: cosines -2 and -1 (no cone)
uniform int uLightSun[8];    // 1: a sun - parallel light along uLightDir, no fading, no cone
uniform int uLightShadow[8]; // the shadow map it casts with (0 .. 3), -1: no shadow
out vec4 o;
#ifdef RF_SHADOWS
// The four shadow maps. GLSL 1.30 cannot pick a sampler by a variable index, so each one has its own
// name: a depth map for a sun or a spotlight, a cube of distances for a lamp.
uniform sampler2D uShadowMap0, uShadowMap1, uShadowMap2, uShadowMap3;
uniform samplerCube uShadowCube0, uShadowCube1, uShadowCube2, uShadowCube3;
uniform mat4 uShadowFromView[4]; // view space -> the depth map's [0, 1] cube (sun, spotlight)
uniform vec2 uShadowLens[4];     // a spotlight's near and far plane (to make its depth linear); 0 0: a sun
uniform float uShadowTexel[4];   // one texel of the depth map, in [0, 1] units
uniform vec3 uShadowLamp[4];     // a lamp's position in the world
uniform float uShadowFar[4];     // a lamp's cube holds distance / uShadowFar
uniform mat4 uViewToWorld;       // the lamps' cubes live in world space

// A depth read from a spotlight's map (0 .. 1, crowded near the far plane) back as a distance along
// its axis: the inverse of the perspective depth, z = 2 n f / (f + n - (2 d - 1)(f - n)).
float linearDepth(float d, vec2 lens) {
    return 2.0 * lens.x * lens.y / (lens.y + lens.x - (2.0 * d - 1.0) * (lens.y - lens.x));
}

// How much of a sun or a spotlight reaches this point: its depth map holds, in every direction from
// the light, the depth of the nearest surface (Williams 1978, "Casting curved shadows on curved
// surfaces"); the point is lit where it is not deeper. The fraction of the 3 x 3 texels around it
// that it is in front of softens the edge a texel wide (percentage-closer filtering, Reeves, Salesin
// & Cook 1987). The bias keeps a lit face from shadowing itself: a little depth for a sun, a few
// centimetres (growing with distance) for a spotlight.
float litByMap(sampler2D map, int s) {
    vec4 h = uShadowFromView[s] * vec4(vPosV, 1.0);
    vec3 p = h.xyz / h.w;
    if (h.w <= 0.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    bool spot = uShadowLens[s].y > 0.0;
    float here = spot ? linearDepth(p.z, uShadowLens[s]) : p.z;
    float bias = spot ? 0.01 + 0.01 * here : 0.003;
    float lit = 0.0;
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j) {
            float stored = texture(map, p.xy + vec2(i, j) * uShadowTexel[s]).r;
            if (spot) stored = linearDepth(stored, uShadowLens[s]);
            lit += here - bias <= stored ? 1.0 : 0.0;
        }
    return lit / 9.0;
}

// How much of a lamp reaches this point: the lamp's cube map holds, in every direction, the distance
// to the nearest surface (omnidirectional shadow maps: Gerasimov, "GPU Gems" ch. 12, 2004); the point
// is lit where it is not farther. Five samples around the direction soften the edge.
float litByCube(samplerCube cube, int s) {
    vec3 w = (uViewToWorld * vec4(vPosV, 1.0)).xyz - uShadowLamp[s];
    float d = max(length(w), 1e-4);
    vec3 a = normalize(cross(w, abs(w.y) < 0.99 * d ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(w / d, a);
    float r = 0.012 * d, bias = 0.02 + 0.02 * d, lit = 0.0;
    vec3 taps[5] = vec3[5](vec3(0.0), a * r, -a * r, b * r, -b * r);
    for (int k = 0; k < 5; ++k) lit += d - bias <= texture(cube, w + taps[k]).r * uShadowFar[s] ? 1.0 : 0.0;
    return lit / 5.0;
}

// The shadow of light i at this point: 1 lit, 0 in shadow. Only the slots drawn this frame are
// compiled in, each with its own kind (RF_SLOTk_MAP: a sun's or a spotlight's depth map, RF_SLOTk_CUBE:
// a lamp's cube; Viewport::buildMeshShader): a lookup that is never used is not free - the software
// renderer runs every branch of a shader, and all eight made each frame ten times slower.
float shadowOf(int i) {
    int s = uLightShadow[i];
#if defined(RF_SLOT0_MAP)
    if (s == 0) return litByMap(uShadowMap0, 0);
#elif defined(RF_SLOT0_CUBE)
    if (s == 0) return litByCube(uShadowCube0, 0);
#endif
#if defined(RF_SLOT1_MAP)
    if (s == 1) return litByMap(uShadowMap1, 1);
#elif defined(RF_SLOT1_CUBE)
    if (s == 1) return litByCube(uShadowCube1, 1);
#endif
#if defined(RF_SLOT2_MAP)
    if (s == 2) return litByMap(uShadowMap2, 2);
#elif defined(RF_SLOT2_CUBE)
    if (s == 2) return litByCube(uShadowCube2, 2);
#endif
#if defined(RF_SLOT3_MAP)
    if (s == 3) return litByMap(uShadowMap3, 3);
#elif defined(RF_SLOT3_CUBE)
    if (s == 3) return litByCube(uShadowCube3, 3);
#endif
    return 1.0;
}
#else
// No light casts a shadow this frame: everything is lit, and the shadow code is left out entirely.
float shadowOf(int i) { return 1.0; }
#endif
vec3 oneLight(vec3 base, vec3 n, vec3 v, vec3 l, vec3 radiance) {
    float diff = max(dot(n, l), 0.0);
    float spec = diff > 0.0 ? pow(max(dot(n, normalize(l + v)), 0.0), 32.0) * 0.25 : 0.0;
    return (base * diff * 0.8 + vec3(spec)) * radiance;
}
vec3 sceneLights(vec3 base, vec3 n, vec3 v) {
    vec3 c = base * (0.18 + 0.1 * max(dot(n, v), 0.0)); // the ambient stays
    for (int i = 0; i < 8; ++i) {
        if (i >= uLightCount) break;
        vec3 l = -uLightDir[i];    // a sun: the same direction everywhere, never fading
        float fade = 1.0, cone = 1.0;
        if (uLightSun[i] == 0) {   // a lamp or a spotlight: from its position, fading with distance
            vec3 toLight = uLightPos[i] - vPosV;
            float d = length(toLight);
            l = toLight / max(d, 1e-5);
            float x = clamp(d / uLightRange[i], 0.0, 1.0);
            fade = (1.0 - x * x) * (1.0 - x * x);
            cone = smoothstep(uLightCosOuter[i], uLightCosInner[i], dot(-l, uLightDir[i]));
        }
        if (fade * cone > 0.0) c += oneLight(base, n, v, l, uLightColor[i]) * fade * cone * shadowOf(i);
    }
    return c;
}
void main() {
    vec3 n = normalize(vN);
    vec3 v = normalize(-vPosV);
    if (dot(n, v) < 0.0) n = -n;
    vec3 base = uUseScalar == 1 ? cmap(uCmap, (vS - uMin) / max(uMax - uMin, 1e-6)) : uColor;
    // Burning fabric (uUseScalar 2, vS = how far it has burnt, 0..1): it browns and chars black,
    // and the zone that is burning right now - the pyrolysis front - glows orange.
    vec3 ember = vec3(0.0);
    if (uUseScalar == 2) {
        base = mix(uColor, vec3(0.35, 0.22, 0.12), smoothstep(0.0, 0.25, vS));
        base = mix(base, vec3(0.05, 0.045, 0.04), smoothstep(0.3, 0.9, vS));
        ember = vec3(1.0, 0.42, 0.08) * 1.6 * smoothstep(0.02, 0.2, vS) * (1.0 - smoothstep(0.75, 1.0, vS));
    }
    vec3 l = normalize(vec3(0.35, 0.7, 0.6));
    float diff = max(dot(n, l), 0.0);
    float fill = max(dot(n, v), 0.0);
    vec3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), 48.0) * 0.3;
    vec3 c = base * (0.18 + 0.55 * diff + 0.35 * fill) + vec3(spec) + ember;
    if (uLightCount > 0) c = sceneLights(base, n, v) + ember;
    // Glass: the rim (grazing view) is brighter, the face almost clear (Schlick's Fresnel).
    float alpha = uAlpha < 1.0 ? uAlpha + (1.0 - uAlpha) * pow(1.0 - max(dot(n, v), 0.0), 4.0) : 1.0;
    o = vec4(c, alpha);
}
)";

// A sun's or a spotlight's shadow map: the depth of every body as the light sees it (nothing else is
// written).
static const char* kShadowVS = R"(in vec3 aPos;
uniform mat4 uLightVP, uModel;
void main() { gl_Position = uLightVP * uModel * vec4(aPos, 1.0); })";
static const char* kShadowFS = R"(out vec4 o;
void main() { o = vec4(1.0); })";

// A lamp's cube of distances, one face at a time: every pixel keeps how far from the lamp the nearest
// surface in its direction is, divided by the lamp's reach (so 1.0 is "nothing there").
static const char* kDistanceVS = R"(in vec3 aPos;
uniform mat4 uLightVP, uModel;
out vec3 vWorld;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    gl_Position = uLightVP * w;
})";
static const char* kDistanceFS = R"(in vec3 vWorld;
uniform vec3 uLamp; uniform float uFar;
out vec4 o;
void main() { o = vec4(length(vWorld - uLamp) / uFar); })";

// Particles as sphere impostors (point sprites shaded as spheres).
static const char* kSphereVS = R"(in vec3 aPos;
in float aScalar;
in vec3 aColor;
in float aRadius;
uniform mat4 uView, uProj;
uniform float uPointScale, uRadiusScale;
out vec3 vCenter; out float vRad; out float vS; out vec3 vCol;
void main() {
    vec4 vp = uView * vec4(aPos, 1.0);
    vCenter = vp.xyz;
    vRad = aRadius * uRadiusScale;
    vS = aScalar;
    vCol = aColor;
    gl_Position = uProj * vp;
    gl_PointSize = max(2.0, 2.0 * vRad * uPointScale / max(-vp.z, 1e-3));
}
)";
static const char* kSphereFS = R"(
in vec3 vCenter; in float vRad; in float vS; in vec3 vCol;
uniform mat4 uProj; uniform int uUseScalar; uniform float uMin, uMax; uniform int uCmap;
out vec4 o;
void main() {
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    c.y = -c.y;
    float r2 = dot(c, c);
    if (r2 > 1.0) discard;
    vec3 n = vec3(c, sqrt(1.0 - r2));
    vec3 pv = vCenter + n * vRad;
    vec4 clip = uProj * vec4(pv, 1.0);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    vec3 base = uUseScalar == 1 && vS > -1e8 ? cmap(uCmap, (vS - uMin) / max(uMax - uMin, 1e-6)) : vCol;
    vec3 l = normalize(vec3(0.35, 0.7, 0.6));
    float diff = max(dot(n, l), 0.0);
    vec3 h = normalize(l + vec3(0, 0, 1));
    float spec = pow(max(dot(n, h), 0.0), 40.0) * 0.35;
    o = vec4(base * (0.25 + 0.5 * diff + 0.3 * n.z) + vec3(spec), 1.0);
}
)";

// A magnetised sphere drawn as an Earth-like planet (procedural - no image files): oceans and
// continents from fractal noise on the sphere, ice caps, drifting clouds, the day side facing the
// Sun (upstream of the plasma wind), an atmosphere rim, sun glint on the oceans, and aurora ovals
// around the magnetic poles (the dipole axis is +y) at ~67 deg magnetic latitude - where the
// field lines that reach the magnetosphere's boundary come down to the surface.

// The magnetised sphere of the magnetosphere scene drawn as a planet (procedural Earth, aurora).
static const char* kPlanetVS = R"(in vec3 aPos;
in vec3 aNormal;
in float aScalar;
uniform mat4 uView, uProj;
out vec3 vWorld;
void main() { vWorld = aPos; gl_Position = uProj * uView * vec4(aPos, 1.0); })";
static const char* kPlanetFS = R"(
in vec3 vWorld;
uniform vec3 uCenter, uEye, uSun;
uniform float uTime;
out vec4 o;
float hash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x), mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x), mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y),
               f.z);
}
float fbm(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 6; ++i) { s += a * noise(p); p = p * 2.03 + vec3(1.7, 9.2, 3.1); a *= 0.5; }
    return s;
}
void main() {
    vec3 n = normalize(vWorld - uCenter);           // direction on the planet = its normal
    vec3 v = normalize(uEye - vWorld);
    float lat = n.y;                                  // sine of the (magnetic) latitude
    // Surface: land where the fractal height is above sea level, ice towards the poles.
    float h = fbm(n * 2.2) - 0.52;
    float ice = smoothstep(0.80, 0.86, abs(lat) + 0.08 * (fbm(n * 6.0) - 0.5));
    vec3 ocean = mix(vec3(0.02, 0.07, 0.20), vec3(0.04, 0.22, 0.38), smoothstep(-0.12, 0.0, h));
    float dry = smoothstep(0.35, 0.65, fbm(n * 3.0 + vec3(5.0)) + 0.3 * (1.0 - abs(lat)));
    vec3 land = mix(vec3(0.13, 0.30, 0.10), vec3(0.55, 0.45, 0.28), dry) * (0.8 + 1.5 * clamp(h, 0.0, 0.2));
    bool isLand = h > 0.0;
    vec3 albedo = mix(isLand ? land : ocean, vec3(0.92, 0.95, 1.0), ice);
    // Clouds drifting eastwards.
    float c = uTime * 0.05;
    vec3 nc = vec3(n.x * cos(c) - n.z * sin(c), n.y, n.x * sin(c) + n.z * cos(c));
    float cloud = smoothstep(0.52, 0.75, fbm(nc * 4.0 + vec3(0.0, 3.0, 0.0)));
    albedo = mix(albedo, vec3(1.0), cloud * 0.85);
    // Sunlight: day and night, glint on open water, atmosphere at the limb.
    float day = max(dot(n, uSun), 0.0);
    float twilight = smoothstep(-0.1, 0.15, dot(n, uSun));
    vec3 col = albedo * (0.03 + 1.1 * day);
    if (!isLand && ice < 0.5) col += vec3(1.0, 0.95, 0.85) * pow(max(dot(n, normalize(uSun + v)), 0.0), 80.0) * 0.6 * (1.0 - cloud) * twilight;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    col += vec3(0.30, 0.55, 1.0) * rim * (0.15 + 0.85 * twilight);
    // Aurora ovals around both magnetic poles, shimmering.
    float oval = exp(-pow((abs(lat) - 0.92) / 0.025, 2.0));
    float curtain = 0.5 + 0.5 * sin(atan(n.z, n.x) * 14.0 + uTime * 1.3 + 6.0 * noise(n * 8.0 + vec3(uTime * 0.3)));
    col += vec3(0.25, 1.0, 0.45) * oval * curtain * (1.0 - 0.7 * day) * 1.4;
    o = vec4(col, 1.0);
}
)";

// Lines: streamlines, field lines, joints, the domain box.
static const char* kLineVS = R"(in vec3 aPos;
in float aScalar;
uniform mat4 uVP;
out float vS;
void main() { vS = aScalar; gl_Position = uVP * vec4(aPos, 1.0); })";
static const char* kLineFS = R"(
in float vS;
uniform vec4 uColor; uniform int uUseScalar; uniform float uMin, uMax; uniform int uCmap;
out vec4 o;
void main() {
    o = uUseScalar == 1 ? vec4(cmap(uCmap, (vS - uMin) / max(uMax - uMin, 1e-6)), uColor.a) : uColor;
}
)";

// A coloured slice of a grid field.
static const char* kSliceVS = R"(in vec3 aPos;
in vec2 aUV;
uniform mat4 uVP;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = uVP * vec4(aPos, 1.0); })";
static const char* kSliceFS = R"(
in vec2 vUV;
uniform sampler2D uTex; uniform float uMin, uMax, uAlpha; uniform int uCmap;
out vec4 o;
void main() {
    vec2 t = texture(uTex, vUV).rg;
    if (t.g > 0.5) { o = vec4(0.22, 0.23, 0.26, 1.0); return; }
    o = vec4(cmap(uCmap, (t.r - uMin) / max(uMax - uMin, 1e-6)), uAlpha);
}
)";

// The smoke / fire / plasma volume: ray marching through the 3D texture against the scene depth.
static const char* kVolVS = R"(in vec3 aPos;
uniform mat4 uVP; uniform vec3 uLo, uSize;
out vec3 vWorld;
void main() { vWorld = uLo + aPos * uSize; gl_Position = uVP * vec4(vWorld, 1.0); })";
// Ray marching through the gas volume: smoke scatters light (self-shadowed); in fire mode the hot
// gas also emits - blackbody colour of its temperature, brightness ~ T^4 (Stefan-Boltzmann) from
// the Draper point (~800 K, the onset of visible glow) up - and the soot darkens what lies behind.
static const char* kVolFS = R"(in vec3 vWorld;
uniform sampler3D uVol; uniform vec3 uEye, uLo, uSize; uniform float uDensity;
uniform int uSteps;          // samples along the ray (fewer on the CPU renderer)
uniform int uFire;           // 1: channel g is the temperature
uniform float uTempScale;    // kelvin above ambient for g = 1
uniform float uAmbient;      // [K]
uniform float uFlame;        // brightness of the flame
uniform sampler2D uDepth; uniform int uUseDepth; uniform mat4 uInvVP; uniform vec2 uViewport; // scene depth
out vec4 o;
// Colour of a black body at temperature T [K], normalised (T. Helland's fit of the Planck locus).
vec3 blackbody(float T) {
    float t = T / 100.0;
    vec3 c;
    c.r = t <= 66.0 ? 1.0 : 1.292936 * pow(t - 60.0, -0.1332047);
    c.g = t <= 66.0 ? 0.3900816 * log(t) - 0.6318414 : 1.1298909 * pow(t - 60.0, -0.0755148);
    c.b = t >= 66.0 ? 1.0 : (t <= 19.0 ? 0.0 : 0.5432068 * log(t - 10.0) - 1.1962541);
    return clamp(c, 0.0, 1.0);
}
void main() {
    vec3 dir = normalize(vWorld - uEye);
    vec3 inv = 1.0 / (dir + vec3(1e-7));
    vec3 t0 = (uLo - uEye) * inv, t1 = (uLo + uSize - uEye) * inv;
    vec3 tmin = min(t0, t1), tmax = max(t0, t1);
    float tn = max(max(max(tmin.x, tmin.y), tmin.z), 0.0);
    float tf = min(min(tmax.x, tmax.y), tmax.z);
    if (uUseDepth == 1) { // stop at the first solid surface of the scene
        vec2 uv = gl_FragCoord.xy / uViewport;
        float z = texture(uDepth, uv).r;
        if (z < 1.0) {
            vec4 w = uInvVP * vec4(uv * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);
            tf = min(tf, dot(w.xyz / w.w - uEye, dir));
        }
    }
    if (tf <= tn) discard;
    float dt = (tf - tn) / float(uSteps);
    vec4 acc = vec4(0.0);
    vec3 glow = vec3(0.0); // emitted light that reaches the eye
    for (int i = 0; i < uSteps; ++i) {
        float t = tn + (float(i) + 0.5) * dt;
        vec3 p = (uEye + dir * t - uLo) / uSize;
        vec2 s = texture(uVol, p).rg;
        float d = s.r;
        if (uFire == 1) {
            float T = uAmbient + s.g * uTempScale;
            if (T > 800.0) {
                float I = uFlame * pow(T / 1500.0, 4.0) * smoothstep(800.0, 1100.0, T);
                glow += (1.0 - acc.a) * blackbody(T) * I * dt;
            }
        }
        if (uFire == 2 && d > 0.004) {
            // Plasma: an optically thin glowing gas - it emits (line radiation of ionised gas,
            // violet-pink as argon / hydrogen discharges) and hardly absorbs.
            glow += vec3(0.62, 0.32, 1.0) * (d * uFlame * dt);
            continue;
        }
        if (d > 0.004) {
            float a = 1.0 - exp(-d * uDensity * dt);
            vec3 col = mix(vec3(0.55, 0.6, 0.68), vec3(0.97, 0.97, 1.0), clamp(d * 1.5, 0.0, 1.0));
            if (uFire == 1) col *= 0.3; // soot: dark grey
            // Self-shadowing: optical depth towards the light over a few samples; the inside of a
            // dense plume darkens and the volume reads as a volume instead of a flat white blob.
            const vec3 L = vec3(0.37, 0.86, 0.35);
            const float ls = 0.04;
            float od = 0.0;
            for (int k = 1; k <= 6; ++k) od += texture(uVol, p + L * (float(k) * ls) / uSize).r;
            col *= 0.3 + 0.7 * exp(-od * uDensity * ls);
            acc.rgb += (1.0 - acc.a) * a * col;
            acc.a += (1.0 - acc.a) * a;
            if (acc.a > 0.98) break;
        }
    }
    // The flame's light is added over what lies behind (premultiplied blending), tone-mapped so
    // that the hot core saturates to white-yellow instead of clipping.
    o = vec4(acc.rgb + (vec3(1.0) - exp(-glow)), acc.a);
}
)";
