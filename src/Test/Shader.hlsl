// --- Constant Buffer ---
// Имена переменных должны совпадать с тем, что мы передаем в SetConstant
cbuffer Constants : register(b0)
{
    float u_Time;
    float4 u_Color;
};

// --- Resources ---
Texture2D tex_Input : register(t0);
SamplerState smp_Linear : register(s0);

// --- Structs ---
struct VS_INPUT
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

// --- Vertex Shader ---
PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;
    // Просто пропускаем координаты квада (-1..1)
    output.pos = float4(input.pos, 1.0f);
    output.uv = input.uv;
    return output;
}

// --- Pixel Shader ---
float4 PSMain(PS_INPUT input) : SV_Target
{
    // Простая анимация на основе UV координат и Времени
    float2 uv = input.uv;

    // Создаем эффект "плазмы"
    float val = sin(uv.x * 10.0 + u_Time) + cos(uv.y * 10.0 + u_Time * 2.0);

    // Смешиваем вычисленное значение с переданным цветом
    float3 finalColor = float3(val * 0.5 + 0.5, uv.x, uv.y) * u_Color.rgb;

    return float4(finalColor, 1.0);
}
