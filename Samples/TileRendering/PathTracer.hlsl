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

// Увеличиваем лимиты для высокой точности
static const int MAX_STEPS = 1000;      // Было 256 -> стало 1000 (чтобы доходить до краев)
static const float MAX_DIST = 100.0;
static const float SURF_DIST = 0.00005; // Было 0.001 -> стало 0.00005 (очень близко к поверхности)

// =========================================================
// 1. PRIMITIVES (SDF)
// =========================================================

float sdSphere(float3 p, float s) { return length(p) - s; }
float sdPlane(float3 p, float3 n, float h) { return dot(p, n) + h; }
float sdBox(float3 p, float3 b) {
    float3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}
float2 opUnion(float2 d1, float2 d2) { return (d1.x < d2.x) ? d1 : d2; }

// =========================================================
// 2. SCENE DEFINITION (STATIC)
// =========================================================

float2 Map(float3 p) {
    float2 scene = float2(MAX_DIST, 0.0);

    // Сфера
    float dSphere = sdSphere(p - float3(-1.2, 1.0, 0.0), 1.0);
    scene = opUnion(scene, float2(dSphere, 1.0));

    // Куб (СТАТИЧНЫЙ, без вращения)
    // Просто сдвигаем его в сторону
    float3 pBox = p - float3(1.5, 1.0, 0.0);
    float dBox = sdBox(pBox, float3(0.8, 0.8, 0.8));
    scene = opUnion(scene, float2(dBox, 2.0));

    // Пол
    float dPlane = sdPlane(p, float3(0.0, 1.0, 0.0), 0.0);
    scene = opUnion(scene, float2(dPlane, 3.0));

    return scene;
}

// =========================================================
// 3. RAY CASTING & NORMAL CALCULATION
// =========================================================

float3 CalcNormal(float3 p) {
    // Epsilon должен быть чуть больше SURF_DIST, но достаточно мал, 
    // чтобы не "захватывать" кривизну соседних объектов.
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
    for (int i = 0; i < MAX_STEPS; i++) {
        float3 p = ro + rd * t;

        // Оптимизация: проверяем только дистанцию (h.x), ID материала не нужен пока
        float h = Map(p).x;

        // Условие попадания
        if (h < SURF_DIST) return t;

        // Безопасный шаг: чуть-чуть не дошагиваем, чтобы не провалиться сквозь геометрию
        // Это убирает артефакты на острых углах
        t += h;

        if (t > MAX_DIST) break;
    }
    return MAX_DIST;
}

// =========================================================
// 4. MAIN SHADER
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
    // 1. Координаты
    float2 uv = input.UV * 2.0 - 1.0;
    uv.y *= -1.0;

    // 2. Луч (из базисных векторов)
    float3 rayDir = normalize(CameraDir.xyz + CameraRight.xyz * uv.x + CameraUp.xyz * uv.y);
    float3 rayOrigin = CameraPos.xyz;

    // 3. Трассировка
    float dist = CastRay(rayOrigin, rayDir);

    // 4. Фон (Небо)
    if (dist >= MAX_DIST) {
        return float4(0.05, 0.05, 0.1, 1.0); // Темно-синий фон
    }

    // 5. Расчет цвета поверхности
    float3 hitPos = rayOrigin + rayDir * dist;
    float3 normal = CalcNormal(hitPos);

    // Освещение (направленный свет)
    float3 lightDir = normalize(float3(-0.5, 0.8, -0.5));
    float diff = max(dot(normal, lightDir), 0.0);

    // Жесткие тени (Raymarched Shadows)
    // Пускаем луч от точки в сторону света
    float shadow = 1.0;
    float t = 0.02; // Начинаем чуть поодаль от поверхности
    for (int i = 0; i < 50; i++) {
        float h = Map(hitPos + lightDir * t).x;
        if (h < 0.001) { shadow = 0.0; break; } // Попали в препятствие -> тень
        t += h;
        if (t > 10.0) break;
    }

    // Albedo (цвет материалов)
    // Чтобы узнать ID материала, вызываем Map еще раз
    float matID = Map(hitPos).y;
    float3 albedo = float3(1,1,1);

    if (matID == 1.0) albedo = float3(0.8, 0.1, 0.1); // Сфера - Красная
    if (matID == 2.0) albedo = float3(0.1, 0.8, 0.2); // Куб - Зеленый
    if (matID == 3.0) albedo = float3(0.5, 0.5, 0.5); // Пол - Серый

    // Финальный цвет
    float3 color = albedo * (diff * shadow + 0.1); // 0.1 ambient

    return float4(color, 1.0);
}
