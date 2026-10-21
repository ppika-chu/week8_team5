#pragma once

enum class EShaderType
{
	Vertex,
	Pixel,
	Compute,
};

enum class EShadowFilter : int32 { None, PCF3x3, PCF5x5, PCSS, Count };
