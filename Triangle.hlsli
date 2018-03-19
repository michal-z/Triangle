#define RootSigDecl \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
	"CBV(b0, visibility = SHADER_VISIBILITY_VERTEX)"

struct ConstData
{
	float4x4 worldToProj;
};
ConstantBuffer<ConstData> s_Cb;


#if defined(VS_TRIANGLE)

[RootSignature(RootSigDecl)]
float4 TriangleVs(float3 pos : POSITION) : SV_POSITION
{
	return mul(float4(pos, 1.0f), s_Cb.worldToProj);
}

#elif defined(PS_TRIANGLE)

[RootSignature(RootSigDecl)]
float4 TrianglePs() : SV_TARGET
{
	return float4(0.0f, 0.5f, 0.0f, 1.0f);
}

#endif