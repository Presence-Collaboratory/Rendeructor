// Шейдер для отрисовки курсора/рамки текущего тайла

cbuffer HighlightParams : register(b0) {
    float TileIndex;
    float TilesStride;
    float TileSize;
    float Padding;
};

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VS_OUTPUT VS_Main(float3 Pos : POSITION, float2 UV : TEXCOORD0) {
    VS_OUTPUT o; o.Pos = float4(Pos, 1); o.UV = UV; return o;
}

float4 PS_Main(VS_OUTPUT input) : SV_Target{
    // Если индекс отрицательный (-1), значит рендер закончен, ничего не рисуем
    if (TileIndex < 0.0) discard;

    int idx = (int)TileIndex;
    int stride = (int)TilesStride;
    float size = TileSize;

    // Считаем координаты текущего тайла
    int tx = idx % stride;
    int ty = idx / stride;

    // Границы в пикселях (Top-Left и Bottom-Right)
    float startX = tx * size;
    float startY = ty * size;
    float endX = startX + size;
    float endY = startY + size;

    float px = input.Pos.x;
    float py = input.Pos.y;

    // Находится ли пиксель ВНУТРИ тайла?
    if (px >= startX && px < endX && py >= startY && py < endY)
    {
        // Дистанция от текущего пикселя до ближайшего края тайла
        float borderDist = min(min(px - startX, endX - px),
                               min(py - startY, endY - py));

        // Рисуем рамку шириной 2px
        if (borderDist <= 2.0) {
            // Зеленый неон (полная непрозрачность)
            return float4(0.0, 1.0, 0.0, 1.0);
        }
        else {
            // Внутренность тайла - чуть подсветим белым, но очень прозрачно (5%)
            // Это создаст эффект "курсора"
            return float4(1.0, 1.0, 1.0, 0.05);
        }
    }

    // Если мы за пределами тайла - полная прозрачность
    return float4(0, 0, 0, 0);
}
