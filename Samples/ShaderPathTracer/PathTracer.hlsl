// =========================================================
// DEFINES & CONSTANTS
// =========================================================

cbuffer SceneBuffer : register(b0) {
    float4 CameraPos;
    float4 CameraDir;
    float4 CameraRight;
    float4 CameraUp;
    float4 Resolution;
    float4 Params; // x = Seed, y = Unused, z = FrameIndex (accumulator)
};

struct SDFObject {
    float4 PositionAndType;
    float4 SizeAndRough;
    float4 RotationAndMetal;
    float4 ColorAndEmit;
};

static const int MAX_OBJECTS = 128;

cbuffer ObjectBuffer : register(b1) {
    SDFObject Objects[MAX_OBJECTS];
    int ObjectCount;
    float3 ObjPadding;
};

Texture2D TexHistory : register(t0);

static const int MAX_MARCH_STEPS = 256;
static const float MAX_DIST = 512.0;
static const float SURF_DIST = 0.001;
static const float PI = 3.14159265359;
static const int MAX_BOUNCES = 16;

// ВАЖНОЕ ИЗМЕНЕНИЕ: 32 или 64. 
// Так как мы рисуем пиксель один раз, нужно сразу набрать качество.
static const int SAMPLES = 64;

// =========================================================
// RNG
// =========================================================
uint WangHash(inout uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u); seed *= 9u;
    seed = seed ^ (seed >> 4u); seed *= 0x27d4eb2d; seed = seed ^ (seed >> 15u);
    return seed;
}
float Rnd1(inout uint seed) { return float(WangHash(seed)) / 4294967295.0; }

uint GetSeed(float2 fragCoord, float seedOffset) {
    // Умножаем float на большое число для псевдо-рандома
    return (uint(fragCoord.x) * 1973u + uint(fragCoord.y) * 9277u + uint(seedOffset * 1234.0)) | 1u;
}

// =========================================================
// SDF & GEOMETRY
// =========================================================
float3 RotatePoint(float3 p, float3 rot) {
    float s, c;
    if (abs(rot.x) > 1e-4) { s = sin(rot.x); c = cos(rot.x); p.yz = mul(float2x2(c, s, -s, c), p.yz); }
    if (abs(rot.y) > 1e-4) { s = sin(rot.y); c = cos(rot.y); p.xz = mul(float2x2(c, -s, s, c), p.xz); }
    if (abs(rot.z) > 1e-4) { s = sin(rot.z); c = cos(rot.z); p.xy = mul(float2x2(c, s, -s, c), p.xy); }
    return p;
}

float SdSphere(float3 p, float s) { return length(p) - s; }
float SdBox(float3 p, float3 b) {
    float3 q = abs(p) - b; return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}
float SdPlane(float3 p) { return dot(p, float3(0, 1, 0)); }

float2 Map(float3 p) {
    float minDist = MAX_DIST;
    float hitIndex = -1.0;
    for (int i = 0; i < ObjectCount; ++i) {
        SDFObject o = Objects[i];
        float3 pos = o.PositionAndType.xyz;
        float3 size = o.SizeAndRough.xyz;
        float3 rot = o.RotationAndMetal.xyz;
        int type = (int)o.PositionAndType.w;

        float3 lp = p - pos;
        if (dot(rot, rot) > 1e-4) lp = RotatePoint(lp, -rot);

        float d = MAX_DIST;
        if (type == 0) d = SdSphere(lp, size.x);
        else if (type == 1) d = SdBox(lp, size);
        else if (type == 2) d = SdPlane(lp);

        if (d < minDist) { minDist = d; hitIndex = (float)i; }
    }
    return float2(minDist, hitIndex);
}

float3 CalcNormal(float3 p) {
    float e = 0.0001; float d = Map(p).x;
    return normalize(float3(d - Map(p - float3(e, 0, 0)).x, d - Map(p - float3(0, e, 0)).x, d - Map(p - float3(0, 0, e)).x));
}

float CastRay(float3 ro, float3 rd) {
    float t = 0;
    for (int i = 0; i < MAX_MARCH_STEPS; i++) {
        float h = Map(ro + rd * t).x;
        if (abs(h) < SURF_DIST) return t;
        t += h;
        if (t > MAX_DIST) break;
    }
    return MAX_DIST;
}

// =========================================================
// PBR BSDF
// =========================================================
float3x3 GetTangentSpace(float3 N) {
    float3 h = abs(N.x) > 0.99 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 T = normalize(cross(N, h)); float3 B = cross(N, T);
    return float3x3(T, B, N);
}
float3 F_Schlick(float cosT, float3 F0) { return F0 + (1.0 - F0) * pow(max(1.0 - cosT, 0.0), 5.0); }
float D_GGX(float NdotH, float r) { float a = r * r; float a2 = a * a; float d = (NdotH * NdotH * (a2 - 1.0) + 1.0); return a2 / (PI * d * d); }
float G_Smith(float NoV, float NoL, float r) { float k = ((r + 1) * (r + 1)) / 8.0; return (NoV / (NoV * (1 - k) + k)) * (NoL / (NoL * (1 - k) + k)); }

float3 SampleGGX(float3 N, float r, inout uint s) {
    float r1 = Rnd1(s), r2 = Rnd1(s); float a = r * r;
    float phi = 2 * PI * r1; float ct = sqrt((1 - r2) / (1 + (a * a - 1) * r2)); float st = sqrt(1 - ct * ct);
    float3 H = float3(cos(phi) * st, sin(phi) * st, ct);
    return mul(H, GetTangentSpace(N));
}
float3 SampleCosine(float3 N, inout uint s) {
    float r1 = Rnd1(s), r2 = Rnd1(s); float phi = 2 * PI * r1; float st = sqrt(r2); float ct = sqrt(1 - r2);
    float3 L = float3(cos(phi) * st, sin(phi) * st, ct);
    return mul(L, GetTangentSpace(N));
}

// =========================================================
// TRACE PATH
// =========================================================
float3 TracePath(float3 ro, float3 rd, inout uint seed) {
    float3 col = 0.0; float3 thr = 1.0;
    for (int b = 0; b < MAX_BOUNCES; b++) {
        float t = CastRay(ro, rd);
        if (t > MAX_DIST - 10.0) {
            float3 sky = float3(0.01, 0.01, 0.015); // Ambient sky
            col += sky * thr; break;
        }

        float3 p = ro + rd * t; float3 n = CalcNormal(p); float3 v = -rd;
        float id = Map(p).y; SDFObject o = Objects[(int)id];
        float3 alb = o.ColorAndEmit.rgb; float emit = o.ColorAndEmit.w;
        float r = max(o.SizeAndRough.w, 0.04); float met = o.RotationAndMetal.w; // Min roughness clamp

        col += emit * alb * thr;
        if (b > 1) { float prob = max(thr.r, max(thr.g, thr.b)); if (Rnd1(seed) > prob) break; thr /= prob; }

        float3 F0 = lerp(0.04, alb, met); float cosT = saturate(dot(n, v));
        float specC = clamp(lerp(F_Schlick(cosT, F0).g, 1.0, met), 0.05, 0.95);

        float3 nextD; float3 bsdf; float pdf;

        if (Rnd1(seed) < specC) { // Specular
            float3 H = SampleGGX(n, r, seed); float3 L = reflect(-v, H);
            float NoL = saturate(dot(n, L)); float NoV = saturate(dot(n, v));
            float NoH = saturate(dot(n, H)); float VoH = saturate(dot(v, H));
            if (NoL > 0) {
                float D = D_GGX(NoH, r); float G = G_Smith(NoV, NoL, r); float3 F = F_Schlick(VoH, F0);
                bsdf = (D * G * F) * NoL / (4.0 * NoV * NoL + 1e-5);
                pdf = (D * NoH) / (4.0 * VoH + 1e-5) * specC; nextD = L;
            }
            else break;
        }
        else { // Diffuse
            nextD = SampleCosine(n, seed); float NoL = saturate(dot(n, nextD));
            float3 F = F_Schlick(saturate(dot(v, normalize(v + nextD))), F0);
            bsdf = (1.0 - F) * (1.0 - met) * (alb / PI) * NoL;
            pdf = (NoL / PI) * (1.0 - specC); nextD = nextD; // nextD
        }
        if (pdf < 1e-6) break;
        thr *= bsdf / pdf;
        ro = p + n * SURF_DIST * 2.0; rd = nextD;
    }
    return col;
}

// =========================================================
// ENTRY POINTS
// =========================================================

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};
struct PS_OUTPUT {
    float4 Color : SV_Target0;
};

VS_OUTPUT VS_Quad(float3 Pos : POSITION, float2 UV : TEXCOORD0) {
    VS_OUTPUT o; o.Pos = float4(Pos, 1); o.UV = UV; return o;
}

#define MAX_RADIANCE 10.0f

PS_OUTPUT PS_PathTrace(VS_OUTPUT input) {
    PS_OUTPUT output = (PS_OUTPUT)0;

    // 1. Генерируем сэмпл
    uint seed = GetSeed(input.Pos.xy, Params.x); // Параметр меняется каждый вызов C++

    float3 newColor = 0;
    for (int s = 0; s < SAMPLES; ++s) {
        seed += s * 1123;
        float2 jt = float2(Rnd1(seed), Rnd1(seed)) - 0.5;
        // Джиттеринг (сглаживание) UV для Anti-Aliasing
        float2 uv = input.UV + (jt / Resolution.xy);
        float2 ndc = uv * 2.0 - 1.0; ndc.y *= -1.0;

        float3 ro = CameraPos.xyz;
        float3 rd = normalize(CameraDir.xyz + CameraRight.xyz * ndc.x + CameraUp.xyz * ndc.y);

        float3 sampleColor = TracePath(ro, rd, seed);

        // 1. Защита от NaN (Бесконечностей), если вдруг вылезли
        if (any(isnan(sampleColor)) || any(isinf(sampleColor))) {
            sampleColor = float3(0, 0, 0);
        }

        // 2. Firefly Clamping
        // Если яркость пикселя безумная (например, 500.0), режем её до MAX_RADIANCE
        // Это не позволит "отравить" среднее значение
        sampleColor = min(sampleColor, float3(MAX_RADIANCE, MAX_RADIANCE, MAX_RADIANCE));

        newColor += sampleColor;
    }
    newColor /= float(SAMPLES);

    // 2. Читаем предыдущий цвет
    // Load быстрее и точнее для попиксельной работы 1 к 1, чем Sample
    float3 prevColor = TexHistory.Load(int3(input.Pos.xy, 0)).rgb;

    // 3. Смешиваем (Moving Average)
    float currentFrame = Params.z; // Передается из C++, 0 для первого кадра

    // Формула: NewAverage = Lerp(OldAverage, NewSample, 1.0 / (N + 1))
    // Если кадр 0, то вес 1.0 (полностью берем новый цвет, игнорируем старый мусор)
    float weight = 1.0 / (currentFrame + 1.0);

    // Защита от NaN (хотя frameIndex в C++ растет)
    if (currentFrame < 0.5) weight = 1.0;

    output.Color = float4(lerp(prevColor, newColor, weight), 1.0);

    return output;
}
