// =========================================================
// DEFINES & CONSTANTS
// =========================================================

cbuffer SceneBuffer : register(b0) {
    float4 CameraPos;
    float4 CameraDir;
    float4 CameraRight;
    float4 CameraUp;
    float4 Resolution;
    float4 Params;
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

static const int MAX_STEPS = 4096;
static const float MAX_DIST = 512;
static const float SURF_DIST = 0.0001;

// =========================================================
// MATH HELPERS
// =========================================================

float3 RotatePoint(float3 p, float3 rot) {
    float s, c;
    // X
    if (abs(rot.x) > 0.0001) {
        s = sin(rot.x); c = cos(rot.x);
        p.yz = mul(float2x2(c, s, -s, c), p.yz);
    }
    // Y
    if (abs(rot.y) > 0.0001) {
        s = sin(rot.y); c = cos(rot.y);
        p.xz = mul(float2x2(c, -s, s, c), p.xz);
    }
    // Z
    if (abs(rot.z) > 0.0001) {
        s = sin(rot.z); c = cos(rot.z);
        p.xy = mul(float2x2(c, s, -s, c), p.xy);
    }
    return p;
}

// =========================================================
// PRIMITIVES
// =========================================================

float sdSphere(float3 p, float s) { return length(p) - s; }
float sdBox(float3 p, float3 b) {
    float3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}
float sdPlane(float3 p, float3 n, float h) { return dot(p, n) + h; }

// =========================================================
// MAP FUNCTION
// =========================================================

float2 Map(float3 p) {
    float minDist = MAX_DIST;
    float hitIndex = -1.0;

    for (int i = 0; i < ObjectCount; i++) {
        SDFObject obj = Objects[i];

        float3 pos = obj.PositionAndType.xyz;
        int type = (int)obj.PositionAndType.w;
        float3 size = obj.SizeAndRough.xyz;
        float3 rot = obj.RotationAndMetal.xyz;

        float3 localP = p - pos;

        // Оптимизация: вращаем только если нужно
        if (dot(rot, rot) > 0.0001) {
            localP = RotatePoint(localP, -rot);
        }

        float d = MAX_DIST;

        if (type == 0) d = sdSphere(localP, size.x);
        else if (type == 1) d = sdBox(localP, size);
        else if (type == 2) d = sdPlane(localP, float3(0, 1, 0), 0.0);

        if (d < minDist) {
            minDist = d;
            hitIndex = (float)i;
        }
    }
    return float2(minDist, hitIndex);
}

// =========================================================
// RAY CASTING
// =========================================================

float3 CalcNormal(float3 p) {
    float e = 0.0001;
    float d = Map(p).x;
    float3 n = float3(
        d - Map(p - float3(e, 0, 0)).x,
        d - Map(p - float3(0, e, 0)).x,
        d - Map(p - float3(0, 0, e)).x
        );
    return normalize(n);
}

float CastRay(float3 ro, float3 rd) {
    float t = 0.0;

    // Бесконечный цикл с жестким брейком
    for (int i = 0; i < MAX_STEPS; i++) {
        float3 p = ro + rd * t;

        // Получаем дистанцию до сцены
        float h = Map(p).x;

        // Если очень близко - мы попали
        if (abs(h) < SURF_DIST) return t;

        t += h; // Шагаем на безопасное расстояние

        // Если улетели за горизонт или попали внутрь геометрии (глюк SDF)
        if (t > MAX_DIST) break;
    }

    return MAX_DIST;
}

// =========================================================
// PIXEL SHADER - SCENE PREVIEW
// =========================================================

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

VS_OUTPUT VS_Quad(float3 Pos : POSITION, float2 UV : TEXCOORD0) {
    VS_OUTPUT output;
    output.Pos = float4(Pos, 1.0);
    output.UV = UV;
    return output;
}

float4 PS_PathTrace(VS_OUTPUT input) : SV_Target{
    float2 uv = input.UV * 2.0 - 1.0;
    uv.y *= -1.0;

    float3 rayDir = normalize(CameraDir.xyz + CameraRight.xyz * uv.x + CameraUp.xyz * uv.y);
    float3 rayOrigin = CameraPos.xyz;

    float dist = CastRay(rayOrigin, rayDir);

    // 1. Фон / Небо
    if (dist >= MAX_DIST - 1.0) {
        return float4(0.01, 0.01, 0.02, 1.0);
    }

    float3 hitPos = rayOrigin + rayDir * dist;
    float3 normal = CalcNormal(hitPos);

    // Распаковка материала
    int index = (int)Map(hitPos).y;
    SDFObject obj = Objects[index];

    float3 albedo = obj.ColorAndEmit.rgb;
    float emission = obj.ColorAndEmit.w;

    // --- ИСПРАВЛЕНИЕ 1: Свет не принимает тени ---
    // Если объект светится достаточно сильно, просто возвращаем его цвет
    if (emission > 0.01) {
        // Albedo * Emission. Плюс можно добавить 1.0 чтобы гарантировать яркость
        // Или просто вернуть float4(albedo * emission, 1.0);
        // Используем такую формулу, чтобы цвет был сочным:
        return float4(albedo * (emission + 1.0), 1.0);
    }

    // --- ОСВЕЩЕНИЕ ДЛЯ ОБЫЧНЫХ ОБЪЕКТОВ ---
    float3 lightPos = float3(8.0, 15.0, 8.0);
    float3 L = normalize(lightPos - hitPos);
    float diff = max(dot(normal, L), 0.0);

    // ТЕНИ
    float shadow = 1.0;
    float distToLight = length(lightPos - hitPos);
    float t_shadow = 0.1; // Bias чуть больше

    for (int k = 0; k < 128; k++) {
        float2 h_res = Map(hitPos + L * t_shadow);
        float h = h_res.x;
        float id = h_res.y; // ID препятствия

        if (h < 0.001) {
            // --- ИСПРАВЛЕНИЕ 2: Лампа не должна отбрасывать тень на других (в превью) ---
            // Если мы врезались в препятствие, проверяем, не лампа ли это?
            // Берем Emission препятствия:
            SDFObject obstacle = Objects[(int)id];
            if (obstacle.ColorAndEmit.w > 0.1) {
                // Если препятствие светится -> это свет, а не тень!
                shadow = 1.0;
            }
 else {
                // Это камень/стена -> это тень
                shadow = 0.0;
            }
            break;
        }

        t_shadow += h;
        if (t_shadow > distToLight) break;
    }

    float3 finalColor = albedo * (diff * shadow + 0.02);
    finalColor += albedo * emission; // (На всякий случай для слабых свечений)

    return float4(finalColor, 1.0);
}
