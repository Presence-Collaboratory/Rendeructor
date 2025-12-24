Texture2D TexHDR;
SamplerState Smp;

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

// Reinhard Tonemapping Operator (Simple)
float3 Reinhard(float3 color) {
    return color / (color + 1.0);
}

// ACES Tonemapping (Cinematic Look)
float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PS_ToneMap(VS_OUTPUT input) : SV_Target{
    // 1. Читаем HDR значение
    float3 hdrColor = TexHDR.Sample(Smp, input.UV).rgb;

    // 2. Экспозиция (можно настраивать)
    float exposure = 1.0;
    hdrColor *= exposure;

    // 3. Tonemapping
    // Превращаем диапазон [0...15.0] в [0...1.0] красиво
    float3 ldrColor = ACESFilm(hdrColor);

    // 4. Gamma Correction (Linear -> sRGB)
    // Мониторы ожидают данные в гамме 2.2
    ldrColor = pow(ldrColor, 1.0 / 2.2);

    return float4(ldrColor, 1.0);
}
