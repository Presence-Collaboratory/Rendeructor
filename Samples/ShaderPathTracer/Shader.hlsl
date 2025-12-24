// =========================================================
// CONSTANT BUFFERS
// =========================================================

// Буфер данных сцены (register b0)
// В C++ это структура SceneData
cbuffer SceneBuffer : register(b0) {
    row_major float4x4 World;
    row_major float4x4 ViewProjection;
    float4 Resolution; // .xy = screen dimensions
    float4 Params;     // .x = time
};

// =========================================================
// INPUT / OUTPUT STRUCTURES
// =========================================================

struct VS_INPUT {
    float3 Pos : POSITION;
    float2 UV  : TEXCOORD0;
};

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

// =========================================================
// VERTEX SHADER (Shared for both passes)
// =========================================================

PS_INPUT VS_Quad(float3 Pos : POSITION, float2 UV : TEXCOORD0) {
    PS_INPUT output;
    output.Pos = float4(Pos, 1.0);
    output.UV = UV;
    return output;
}

// =========================================================
// RAYMARCHING LOGIC (SDF)
// =========================================================

// Вращение вектора вокруг оси Y
float3 RotateY(float3 p, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return float3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

// Вращение вектора вокруг оси X
float3 RotateX(float3 p, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return float3(p.x, c * p.y - s * p.z, s * p.y + c * p.z);
}

// Signed Distance Function для Коробки
float sdBox(float3 p, float3 b) {
    float3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// Карта мира: здесь описываем геометрию
float Map(float3 p) {
    float time = Params.x;

    // Вращаем пространство
    p = RotateY(p, time);
    p = RotateX(p, time * 0.6);

    // Куб размером 1.0
    return sdBox(p, float3(1.0, 1.0, 1.0));
}

// Вычисление нормали для освещения
float3 CalculateNormal(float3 p) {
    float e = 0.001;
    float d = Map(p);
    float3 n = float3(
        d - Map(p - float3(e, 0, 0)),
        d - Map(p - float3(0, e, 0)),
        d - Map(p - float3(0, 0, e))
        );
    return normalize(n);
}

// =========================================================
// PIXEL SHADER: RAYMARCHING (Pass 1)
// =========================================================

float4 PS_Scene_Raymarch(PS_INPUT input) : SV_Target{
    // Преобразуем UV из [0..1] в [-1..1]
    float2 uv = input.UV * 2.0 - 1.0;

    // Корректируем соотношение сторон
    float aspect = Resolution.x / Resolution.y;
    uv.x *= aspect;

    // Настройка камеры (Ray Origin, Ray Direction)
    float3 ro = float3(0.0, 0.0, -3.5); // Камера сдвинута назад по Z
    float3 rd = normalize(float3(uv, 1.0)); // Луч идет вперед

    float t = 0.0;       // Дистанция полета луча
    float tMax = 20.0;   // Максимальная дальность

    // Цвет фона (Черный, 0 альфа)
    float3 color = float3(0.0, 0.0, 0.0);

    // Raymarching Loop
    [unroll(100)]
    for (int i = 0; i < 64; i++) {
        float3 p = ro + rd * t;
        float d = Map(p);

        // Попали в поверхность?
        if (d < 0.001) {
            float3 normal = CalculateNormal(p);

            // Простое освещение
            float3 lightDir = normalize(float3(0.5, 1.0, -1.0));
            float diff = max(dot(normal, lightDir), 0.0);

            // Цвет куба (Красный)
            color = float3(1.0, 0.0, 0.0) * (diff + 0.2); // +0.2 Ambient
            break;
        }

        t += d;
        if (t > tMax) break;
    }

    return float4(color, 1.0);
}

// =========================================================
// PIXEL SHADER: POST PROCESS (Pass 2)
// =========================================================

Texture2D InputTexture : register(t0);
SamplerState InputSampler : register(s0);

float4 PS_PostProcess(PS_INPUT input) : SV_Target{
    // 1. Читаем результат Pass 1
    float4 color = InputTexture.Sample(InputSampler, input.UV);

    // 2. Эффект: Инверсия цвета
    // Фон: (0,0,0) -> (1,1,1) (Белый)
    // Куб: (1,0,0) -> (0,1,1) (Голубой)
    float3 finalColor = 1.0 - color.rgb;

    // 3. Эффект: Виньетка (затемнение краев)
    float2 center = input.UV * 2.0 - 1.0; // [-1..1]
    float dist = dot(center, center); // Дистанция от центра в квадрате
    float vignette = 1.0 - dist * 0.4;
    vignette = saturate(vignette);

    finalColor *= vignette;

    return float4(finalColor, 1.0);
}
