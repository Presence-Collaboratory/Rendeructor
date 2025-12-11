cbuffer Constants : register(b0)
{
    // ВАЖНО: row_major говорит шейдеру, что матрица лежит в памяти строками
    // Это предотвращает автоматическое транспонирование
    row_major float4x4 u_MVP;
};

Texture2D tex_Diffuse : register(t0);
SamplerState smp_Main : register(s0);

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

PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;

    float4 localPos = float4(input.pos, 1.0f);

    // ВАЖНО: Для Row-Major матриц порядок: mul(vector, matrix)
    output.pos = mul(localPos, u_MVP);

    output.uv = input.uv;
    return output;
}

float4 PSMain(PS_INPUT input) : SV_Target
{
    return tex_Diffuse.Sample(smp_Main, input.uv);
}