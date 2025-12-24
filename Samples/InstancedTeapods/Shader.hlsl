// =================================================================================
// DEFINES & CONSTANT BUFFERS
// =================================================================================

cbuffer TransformBuffer : register(b0) {
    row_major float4x4 World;
    row_major float4x4 ViewProjection;      // Матрица камеры (или Света при ShadowPass)
    row_major float4x4 LightViewProjection; // Матрица Света (для расчета теней в Combine)
};

cbuffer SSAOConfigBuffer : register(b1) {
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
    float4   Resolution;
    float4   CameraPosition;
    float4   Kernel[64];
};

struct VS_INPUT_MESH {
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
    float2 UV  : TEXCOORD0;
};

struct PS_INPUT_MESH {
    float4 Pos : SV_POSITION;
    float3 WorldPos : POSITION0;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct PS_INPUT_SHADOW {
    float4 Pos : SV_POSITION;
    float4 DepthPos : TEXCOORD0;
};

struct PS_INPUT_QUAD {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

// =================================================================================
// VERTEX SHADERS
// =================================================================================

struct VS_INSTANCED_INPUT {
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
    float2 UV  : TEXCOORD0;

    // ДАННЫЕ ИНСТАНСА (МАТРИЦА 4x4)
    // HLSL ожидает матрицу как 4 вектора float4.
    // Семантика "INSTANCE_" позволяет Rendeructor понять, что это slot #1 (Instancing)
    float4 InstRow0 : INSTANCE_WORLD0;
    float4 InstRow1 : INSTANCE_WORLD1;
    float4 InstRow2 : INSTANCE_WORLD2;
    float4 InstRow3 : INSTANCE_WORLD3;
};

// 1. Стандартный для Мешей (G-Buffer)
PS_INPUT_MESH VS_MeshInstanced(VS_INSTANCED_INPUT input) {
    PS_INPUT_MESH output;

    // Собираем матрицу мира из входных векторов инстанса
    float4x4 instanceWorld = float4x4(input.InstRow0, input.InstRow1, input.InstRow2, input.InstRow3);

    // Дальше все как обычно, только используем instanceWorld вместо World
    float4 wPos = mul(float4(input.Pos, 1.0), instanceWorld);
    output.WorldPos = wPos.xyz;
    output.Pos = mul(wPos, ViewProjection);
    output.Normal = normalize(mul(input.Normal, (float3x3)instanceWorld));
    output.UV = input.UV;
    return output;
}

PS_INPUT_MESH VS_Mesh(VS_INPUT_MESH input) {
    PS_INPUT_MESH output;
    float4 wPos = mul(float4(input.Pos, 1.0), World);
    output.WorldPos = wPos.xyz;
    output.Pos = mul(wPos, ViewProjection);
    output.Normal = normalize(mul(input.Normal, (float3x3)World));
    output.UV = input.UV;
    return output;
}

// 2. Для генерации Shadow Map
PS_INPUT_SHADOW VS_Shadow(VS_INPUT_MESH input) {
    PS_INPUT_SHADOW output;
    float4 wPos = mul(float4(input.Pos, 1.0), World);
    // Здесь ViewProjection - это матрица Света (LightView * LightProj)
    output.Pos = mul(wPos, ViewProjection);
    output.DepthPos = output.Pos;
    return output;
}

PS_INPUT_SHADOW VS_ShadowInstanced(VS_INSTANCED_INPUT input) {
    PS_INPUT_SHADOW output;

    float4x4 instanceWorld = float4x4(input.InstRow0, input.InstRow1, input.InstRow2, input.InstRow3);

    float4 wPos = mul(float4(input.Pos, 1.0), instanceWorld);

    // ViewProjection здесь — это матрица СВЕТА
    output.Pos = mul(wPos, ViewProjection);
    output.DepthPos = output.Pos;
    return output;
}

// 3. Для полноэкранного квадрата
PS_INPUT_QUAD VS_Quad(float3 Pos : POSITION, float2 UV : TEXCOORD0) {
    PS_INPUT_QUAD output;
    output.Pos = float4(Pos, 1.0);
    output.UV = UV;
    return output;
}

// =================================================================================
// PIXEL SHADER: SHADOW GENERATION (Pass 0)
// =================================================================================

float4 PS_Shadow(PS_INPUT_SHADOW input) : SV_Target{
    // Пишем линейную глубину в текстуру R32F
    float depth = input.DepthPos.z / input.DepthPos.w;
    return float4(depth, depth, depth, 1.0);
}

// =================================================================================
// PIXEL SHADER: G-BUFFER (Pass 1)
// =================================================================================

struct GBUFFER_OUTPUT {
    float4 Albedo   : SV_Target0; // RGB = Color, A = Specular Intensity
    float4 Position : SV_Target1;
    float4 Normal   : SV_Target2;
};

GBUFFER_OUTPUT PS_GBuffer(PS_INPUT_MESH input) {
    GBUFFER_OUTPUT output;

    // Шахматка
    float scale = 100.0f;
    bool isFloor = input.WorldPos.y < -0.5;

    if (isFloor) scale = 10.0;

    float pattern = (step(0.5, frac(input.UV.x * scale)) + step(0.5, frac(input.UV.y * scale)));
    float3 color = (fmod(pattern, 2.0) > 0.5) ? float3(0.9, 0.9, 0.9) : float3(0.4, 0.0, 0.0);

    // --- НАСТРОЙКА МАТЕРИАЛОВ ---
    output.Position = float4(input.WorldPos, 1.0);
    output.Normal = float4(normalize(input.Normal), 1.0);

    // В альфа-канал пишем "Силу блеска" (Specular Intensity)
    // Чайник (не пол) пусть блестит сильно (0.8), пол слабый (0.1)
    float shininess = isFloor ? 0.1 : 0.9;
    output.Albedo = float4(color, shininess);

    return output;
}

// =================================================================================
// PIXEL SHADER: SSAO RAW (Pass 2)
// =================================================================================

Texture2D TexPosition : register(t0);
Texture2D TexNormal   : register(t1);
Texture2D TexNoise    : register(t2);

SamplerState SamplerClamp : register(s0);
SamplerState SamplerPoint : register(s1);

float4 PS_SSAO_Raw(PS_INPUT_QUAD input) : SV_Target{
    float3 fragPos = TexPosition.Sample(SamplerClamp, input.UV).xyz;
    float3 normal = normalize(TexNormal.Sample(SamplerClamp, input.UV).xyz);

    // Если позиция (0,0,0) (фон), не затеняем
    if (length(fragPos) < 0.1) return 1.0;

    float2 noiseScale = Resolution.xy / 4.0; // Повторяем 4x4 текстуру
    float3 randomVec = TexNoise.Sample(SamplerPoint, input.UV * noiseScale).xyz;

    float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    float3 bitangent = cross(normal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, normal);

    float occlusion = 0.0;
    float radius = 0.5;
    float bias = 0.025;
    int kernelSize = 64;

    [unroll(32)]
    for (int i = 0; i < kernelSize; ++i)
    {
        float3 samplePos = mul(Kernel[i].xyz, TBN);
        samplePos = fragPos + samplePos * radius;

        float4 offset = float4(samplePos, 1.0);
        offset = mul(offset, ViewMatrix);
        offset = mul(offset, ProjectionMatrix);
        offset.xyz /= offset.w;
        float2 offsetUV = offset.xy * float2(0.5, -0.5) + 0.5;

        // Clip Check
        if (offsetUV.x < 0 || offsetUV.y < 0 || offsetUV.x > 1 || offsetUV.y > 1) continue;

        float3 occluderPos = TexPosition.Sample(SamplerClamp, offsetUV).xyz;
        if (length(occluderPos) < 0.01) continue;

        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - occluderPos.z));
        // Для теста просто distance, т.к. позиции в World Space
        rangeCheck = smoothstep(0.0, 1.0, radius / distance(fragPos, occluderPos));

        float distSample = distance(CameraPosition.xyz, samplePos);
        float distOccluder = distance(CameraPosition.xyz, occluderPos);

        if (distOccluder <= distSample - bias) {
            occlusion += 1.0 * rangeCheck;
        }
    }

    occlusion = 1.0 - (occlusion / kernelSize);
    return float4(occlusion, occlusion, occlusion, 1.0);
}

// =================================================================================
// PIXEL SHADER: DENOISE (Pass 3)
// =================================================================================

Texture2D TexSSAO_Raw : register(t0);

float4 PS_Denoise(PS_INPUT_QUAD input) : SV_Target{
    float2 texelSize = 1.0 / Resolution.xy;
    float result = 0.0;
    // Box blur 4x4
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            result += TexSSAO_Raw.Sample(SamplerClamp, input.UV + float2(x,y) * texelSize).r;
        }
    }
    return result / 16.0;
}

// =================================================================================
// LIGHTING HELPER FUNCTIONS
// =================================================================================

// Данные о свете (должны совпадать с test.cpp)
// Чтобы не плодить константы, пока захардкодим их здесь так же, как в test.cpp.
static const float3 LightPos = float3(10.0f, 15.0f, -10.0f);
static const float3 LightColor = float3(1.0f, 0.95f, 0.8f); // Теплый солнечный свет
static const float LightIntensity = 1.3f;
static const float AmbientIntensity = 0.3f;

Texture2D TexShadow;

float CalculateShadowPCF(float3 worldPos, float3 normal, float3 lightDir) {
    // 1. Transform World -> Light Clip Space
    float4 lightSpacePos = mul(float4(worldPos, 1.0), LightViewProjection);

    // 2. Perspective divide -> NDC
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // 3. NDC -> UV
    float2 shadowUV;
    shadowUV.x = projCoords.x * 0.5 + 0.5;
    shadowUV.y = -projCoords.y * 0.5 + 0.5;

    // Проверка границ
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
        return 1.0;
    if (projCoords.z > 1.0) return 1.0;

    // --- SMART BIAS ---
    // Вычисляем bias в зависимости от угла наклона поверхности к свету.
    // dot(normal, lightDir) дает косинус угла.
    // Если угол 90 град, dot=0, нам нужен больший bias.
    // Если угол 0 град (свет прямо), dot=1, bias минимален.
    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);

    // --- PCF 3x3 ---
    float shadow = 0.0;

    // Размер текселя тени. У нас карта 2048, значит 1/2048.
    float2 texelSize = 1.0 / 2048.0;

    // Цикл 3x3
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = TexShadow.Sample(SamplerClamp, shadowUV + float2(x, y) * texelSize).r;
            // Сравнение глубины (1.0 = Свет, 0.0 = Тень)
            shadow += (currentDepth - bias > pcfDepth) ? 0.0 : 1.0;
        }
    }

    // Усредняем (9 выборок)
    shadow /= 9.0;

    return shadow;
}

// =================================================================================
// PIXEL SHADER: COMBINE (Final High Quality Shading)
// =================================================================================

Texture2D TexAlbedo; // .rgb = Color, .a = Spec Intensity
Texture2D TexSSAO;
Texture2D TexPosWorld; // World Pos (понадобится для ViewDir)
                                        // NB: Лучше было бы брать Normal из GBuffer (t4), 
                                        // но у нас там t2 шум занял? 
                                        // А, нет, в test.cpp Normal в Combined Pass не передан явно?
                                        // !! ВАЖНО: В test.cpp добавьте Normal в CombinePass!
                                        // Или мы восстановим нормаль ddx/ddy? Нет, качество плохое.
                                        // Добавьте Normal texture register(t4).
                                        // Для текущего примера пока попробуем ddx/ddy для нормали или добавим TexNormal.

// Добавим текстуру нормали в Combined шейдер
Texture2D TexNormalWorld;

float4 PS_Combine(PS_INPUT_QUAD input) : SV_Target{
    // 1. Читаем G-Buffer
    float4 albedoData = TexAlbedo.Sample(SamplerClamp, input.UV);
    float3 albedo = albedoData.rgb;
    float  specIntensity = albedoData.a; // Взяли из альфа-канала

    float ssao = TexSSAO.Sample(SamplerClamp, input.UV).r;
    float3 worldPos = TexPosWorld.Sample(SamplerClamp, input.UV).xyz;

    // Важно: Нормаль распаковываем/читаем. 
    // Предполагаем, что test.cpp передаст TexNormal в CombinePass.
    float3 normal = TexNormalWorld.Sample(SamplerClamp, input.UV).xyz;

    // Если нормали нет (фон), она (0,0,0)
    if (length(normal) < 0.1) return float4(albedo, 1.0); // Фон

    // 2. Вектора
    float3 lightDir = normalize(LightPos - worldPos); // Directional light logic typically uses fixed direction, but Pos works for point too. Let's treat as Pos.
    // Если это направленный свет (как солнце), то lightDir = normalize(LightPos - Target). 
    // В test.cpp он направлен примерно от (10, 15, -10) в (0,0,0).
    // Будем считать Directional для качества теней.
    lightDir = normalize(float3(0.5f, 0.8f, -0.5f));

    float3 viewDir = normalize(CameraPosition.xyz - worldPos);
    float3 halfwayDir = normalize(lightDir + viewDir);

    // 3. Shadow Mapping
    float shadow = CalculateShadowPCF(worldPos, normal, lightDir);

    // 4. Освещение (Blinn-Phong)

    // A. Ambient (окружающий свет * SSAO)
    // Делаем тень в Ambient не черной, а слегка голубоватой (имитация неба)
    float3 ambient = float3(0.3, 0.35, 0.4) * albedo * ssao * AmbientIntensity;

    // B. Diffuse (Ламберт)
    float diff = max(dot(normal, lightDir), 0.0);
    float3 diffuse = diff * LightColor * LightIntensity * albedo;

    // C. Specular (Блик)
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0); // 32 - жесткость блика
    float3 specular = float3(1,1,1) * spec * specIntensity * LightIntensity;

    // Итоговый цвет
    // Ambient всегда есть. Diffuse и Specular зависят от тени.
    float3 finalColor = ambient + (diffuse + specular) * shadow;

    // Небольшой Tonemapping (Reinhard) чтобы не выжигало глаза
    finalColor = finalColor / (finalColor + 1.0);
    // Gamma correction
    finalColor = pow(finalColor, 1.0 / 2.2);

    return float4(finalColor, 1.0);
}
