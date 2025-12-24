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

// Структура объекта (совпадает с C++)
struct SDFObject {
    float4 PositionAndType; // .w = Type
    float4 SizeAndPadding;
    float4 Rotation;
    float4 Color;
};

static const int MAX_OBJECTS = 64;

// Буфер объектов (register b1)
cbuffer ObjectBuffer : register(b1) {
    SDFObject Objects[MAX_OBJECTS];
    int ObjectCount;
    float3 ObjPadding;
};

// Лимиты
static const int MAX_STEPS = 512;
static const float MAX_DIST = 100.0;
static const float SURF_DIST = 0.0001;

// =========================================================
// MATH HELPERS
// =========================================================

// Вращение точки p на углы Эйлера (inverse rotation для объекта)
float3 RotatePoint(float3 p, float3 rot) {
    // Rotate X
    float s = sin(rot.x); float c = cos(rot.x);
    p.yz = mul(float2x2(c, s, -s, c), p.yz);
    // Rotate Y
    s = sin(rot.y); c = cos(rot.y);
    p.xz = mul(float2x2(c, -s, s, c), p.xz);
    // Rotate Z
    s = sin(rot.z); c = cos(rot.z);
    p.xy = mul(float2x2(c, s, -s, c), p.xy);
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
// SCENE MAPPING (DYNAMIC LOOP)
// =========================================================

// Возвращает float2(Distance, ObjectIndex)
// Теперь .y это не абстрактный ID материала, а ИНДЕКС в массиве Objects
float2 Map(float3 p) {

    float minDist = MAX_DIST;
    float hitIndex = -1.0;

    for (int i = 0; i < ObjectCount; i++) {
        SDFObject obj = Objects[i];

        float3 pos = obj.PositionAndType.xyz;
        int type = (int)obj.PositionAndType.w;
        float3 size = obj.SizeAndPadding.xyz;
        float3 rot = obj.Rotation.xyz;

        // Трансформируем точку в локальное пространство объекта
        // 1. Сдвиг (Translation)
        float3 localP = p - pos;

        // 2. Вращение (Rotation) - только если углы не нулевые (для оптимизации можно убрать if)
        // Для сферы вращение не нужно, но для общности оставим
        if (length(rot) > 0.001) {
            // Вращаем пространство в ОБРАТНУЮ сторону вращения объекта
            localP = RotatePoint(localP, -rot);
        }

        float d = MAX_DIST;

        if (type == 0) { // SPHERE
            // Для сферы size.x = radius. Вращение не влияет на форму, но влияет на текстуру (если бы была)
            // Но мы вращаем localP, так что все ок.
            d = sdSphere(localP, size.x);
        }
        else if (type == 1) { // BOX
            d = sdBox(localP, size);
        }
        else if (type == 2) { // PLANE
            // Для плоскости rotation задает нормаль? 
            // Пока упростим: плоскость всегда смотрит вверх Y, а pos.y задает высоту
            // Если нужно вращать плоскость, localP это обработает.
            // Нормаль в локальном пространстве всегда (0,1,0)
            d = sdPlane(localP, float3(0, 1, 0), 0.0);
        }

        // Объединение (Union)
        if (d < minDist) {
            minDist = d;
            hitIndex = (float)i;
        }
    }

    return float2(minDist, hitIndex);
}

// =========================================================
// RAY CASTING & NORMALS
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
    // Оптимизация шагов для цикла
    for (int i = 0; i < MAX_STEPS; i++) {
        float3 p = ro + rd * t;
        float h = Map(p).x;
        if (h < SURF_DIST) return t;
        t += h;
        if (t > MAX_DIST) break;
    }
    return MAX_DIST;
}

// =========================================================
// PIXEL SHADER
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

    if (dist >= MAX_DIST) {
        return float4(0.05, 0.05, 0.1, 1.0);
    }

    float3 hitPos = rayOrigin + rayDir * dist;
    float3 normal = CalcNormal(hitPos);

    // --- ОПРЕДЕЛЕНИЕ ЦВЕТА ОБЪЕКТА ---
    // 1. Узнаем индекс объекта, в который попали
    float index = Map(hitPos).y;

    float3 albedo = float3(1,0,1); // Розовый (ошибка) по умолчанию

    // 2. Достаем цвет из массива по индексу
    if (index >= 0.0) {
        // HLSL позволяет индексировать массивы неконстантным индексом
        // (хотя это может быть чуть медленнее, для <64 объектов это моментально)
        int i = (int)index;
        albedo = Objects[i].Color.rgb;
    }

    // --- ОСВЕЩЕНИЕ ---
    float3 lightDir = normalize(float3(-0.5, 0.8, -0.5));
    float diff = max(dot(normal, lightDir), 0.0);

    // Тени
    float shadow = 1.0;
    float t = 0.02;
    for (int j = 0; j < 50; j++) {
        float h = Map(hitPos + lightDir * t).x;
        if (h < 0.001) { shadow = 0.0; break; }
        t += h;
        if (t > 10.0) break;
    }

    float3 color = albedo * (diff * shadow + 0.1);

    // Гамма коррекция (примитивная)
    color = pow(color, 1.0 / 2.2);

    return float4(color, 1.0);
}
