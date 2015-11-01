// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cmath>

#include "VideoCommon/HullDomainShaderGen.h"
#include "VideoCommon/LightingShaderGen.h"
#include "VideoCommon/VertexShaderGen.h"
#include "VideoCommon/VideoConfig.h"
static char text[HULLDOMAINSHADERGEN_BUFFERSIZE];

static const char* headerUtilI = R"hlsl(
int4 CHK_O_U8(int4 x)
{
	return x & 255;
}
#define BOR(x, n) ((x) | (n))
#define BSHR(x, n) ((x) >> (n))
int2 BSH(int2 x, int n)
{
	if(n >= 0)
	{
		return x >> n;
	}
	else
	{
		return x << (-n);
	}
}
int remainder(int x, int y)
{
	return x %% y;
}
// dot product for integer vectors
int idot(int3 x, int3 y)
{
	int3 tmp = x * y;
	return tmp.x + tmp.y + tmp.z;
}
int idot(int4 x, int4 y)
{
	int4 tmp = x * y;
	return tmp.x + tmp.y + tmp.z + tmp.w;
}
// rounding + casting to integer at once in a single function
int  wuround(float  x) { return int(round(x)); }
int2 wuround(float2 x) { return int2(round(x)); }
int3 wuround(float3 x) { return int3(round(x)); }
int4 wuround(float4 x) { return int4(round(x)); }
)hlsl";

static const char* s_hlsl_header_str = R"hlsl(
struct HSOutput
{
	float4 pos: BEZIERPOS;
};

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("TConstFunc")]
HSOutput HS_TFO(InputPatch<VS_OUTPUT, 3> patch, uint id : SV_OutputControlPointID,uint patchID : SV_PrimitiveID)
{
HSOutput result = (HSOutput)0;
return result;
}

float GetScreenSize(float3 Origin, float Diameter)
{
    float w = dot()hlsl" I_PROJECTION R"hlsl([3], float4( Origin, 1.0 ));
    return abs(Diameter * )hlsl" I_PROJECTION R"hlsl([1].y / w);
}

float CalcTessFactor(float3 Origin, float Diameter)
{
    return round(max()hlsl" I_TESSPARAMS ".x, " I_TESSPARAMS R"hlsl(.y * GetScreenSize(Origin,Diameter)));
}
ConstantOutput TConstFunc(InputPatch<VS_OUTPUT, 3> patch)
{
ConstantOutput result = (ConstantOutput)0;
)hlsl";

static const char* s_hlsl_ds_str = R"hlsl(
float3 PrjToPlane(float3 planeNormal, float3 planePoint, float3 pointToProject)
{
return pointToProject - dot(pointToProject-planePoint, planeNormal) * planeNormal;
}

float2 BInterpolate(float2 v0, float2 v1, float2 v2, float3 barycentric)
{
return barycentric.z * v0 + barycentric.x * v1 + barycentric.y * v2;
}

float2 BInterpolate(float2 v[3], float3 barycentric)
{
return BInterpolate(v[0], v[1], v[2], barycentric);
}

float3 BInterpolate(float3 v0, float3 v1, float3 v2, float3 barycentric)
{
return barycentric.z * v0 + barycentric.x * v1 + barycentric.y * v2;
}

float3 BInterpolate(float3 v[3], float3 barycentric)
{
return BInterpolate(v[0], v[1], v[2], barycentric);
}

float4 BInterpolate(float4 v0, float4 v1, float4 v2, float3 barycentric)
{
return barycentric.z * v0 + barycentric.x * v1 + barycentric.y * v2;
}

float4 BInterpolate(float4 v[3], float3 barycentric)
{
return BInterpolate(v[0], v[1], v[2], barycentric);
}

[domain("tri")]
VS_OUTPUT DS_TFO(ConstantOutput pconstans, const OutputPatch<HSOutput, 3> patch, float3 bCoords : SV_DomainLocation )
{
VS_OUTPUT result = (VS_OUTPUT)0;
)hlsl";



template<class T, API_TYPE ApiType>
void SampleTextureRAW(T& out, const char *texcoords, const char *texswap, const char *layer, int texmap, int tcoord)
{
	if (ApiType == API_D3D11)
	{
		out.Write("Tex[%d].SampleLevel(samp[%d], float3(%s.xy * " I_TEXDIMS"[%d].xy, %s), log2(1.0 / " I_TEXDIMS "[%d].x) * uv[%d].w).%s;\n", 8 + texmap, texmap, texcoords, texmap, layer, texmap, tcoord, texswap);
	}
	else
	{
		out.Write("texture(samp[%d],float3(%s.xy * " I_TEXDIMS"[%d].xy, %s)).%s;\n", 8 + texmap, texcoords, texmap, layer, texswap);
	}
}

template<class T, API_TYPE ApiType>
void SampleTexture(T& out, const char *texcoords, const char *texswap, int texmap)
{
	if (ApiType == API_D3D11)
	{
		out.Write("wuround((Tex[%d].SampleLevel(samp[%d], float3(%s.xy * " I_TEXDIMS"[%d].xy, %s), 0.0)).%s * 255.0);\n", texmap, texmap, texcoords, texmap, g_ActiveConfig.iStereoMode > 0 ? "layer" : "0.0", texswap);
	}
	else if (ApiType == API_OPENGL)
	{
		out.Write("wuround(texture(samp[%d],float3(%s.xy * " I_TEXDIMS"[%d].xy, %s)).%s * 255.0);\n", texmap, texcoords, texmap, g_ActiveConfig.iStereoMode > 0 ? "layer" : "0.0", texswap);
	}
	else
	{
		out.Write("wuround(tex2D(samp[%d],%s.xy * " I_TEXDIMS"[%d].xy).%s * 255.0);\n", texmap, texcoords, texmap, texswap);
	}
}

static inline void WriteStageUID(HullDomain_shader_uid_data& uid_data, int n, const BPMemory &bpm)
{
	int texcoord = bpm.tevorders[n / 2].getTexCoord(n & 1);
	bool bHasTexCoord = (u32)texcoord < bpm.genMode.numtexgens.Value();
	bool bHasIndStage = bpm.tevind[n].bt < bpm.genMode.numindstages.Value();
	// HACK to handle cases where the tex gen is not enabled
	if (!bHasTexCoord)
		texcoord = 0;
	uid_data.stagehash[n].hasindstage = bHasIndStage;
	uid_data.stagehash[n].tevorders_texcoord = texcoord;
	if (bHasIndStage)
	{
		uid_data.stagehash[n].tevind = bpm.tevind[n].hex & 0x7FFFFF;
	}
	const int i = bpm.combiners[n].alphaC.tswap;
	uid_data.stagehash[n].tevorders_enable = bpm.tevorders[n / 2].getEnable(n & 1);
	if (bpm.tevorders[n / 2].getEnable(n & 1))
	{
		int texmap = bpm.tevorders[n / 2].getTexMap(n & 1);
		uid_data.stagehash[n].tevorders_texmap = texmap;
		uid_data.SetTevindrefTexmap(i, texmap);
	}
}

template<class T, API_TYPE ApiType>
static inline void WriteFetchDisplacement(T& out, int n, const BPMemory &bpm)
{
	int texcoord = bpm.tevorders[n / 2].getTexCoord(n & 1);
	bool bHasTexCoord = (u32)texcoord < bpm.genMode.numtexgens.Value();
	bool bHasIndStage = bpm.tevind[n].bt < bpm.genMode.numindstages.Value();
	// HACK to handle cases where the tex gen is not enabled
	if (!bHasTexCoord)
		texcoord = 0;
	out.Write("\n{\n");

	if (bpm.tevorders[n / 2].getEnable(n & 1))
	{
		int texmap = bpm.tevorders[n / 2].getTexMap(n & 1);
		out.Write("if((" I_FLAGS ".x & %i) != 0)\n{\n", 1 << texmap);
		if (bHasIndStage)
		{
			out.Write("// indirect op\n");
			// perform the indirect op on the incoming regular coordinates using indtex%d as the offset coords
			if (bpm.tevind[n].mid != 0)
			{
				static const char *tevIndFmtMask[] = { "255", "31", "15", "7" };
				out.Write("int3 indtevcrd%d = indtex%d & %s;\n", n, bpmem.tevind[n].bt, tevIndFmtMask[bpmem.tevind[n].fmt]);

				static const char *tevIndBiasField[] = { "", "x", "y", "xy", "z", "xz", "yz", "xyz" }; // indexed by bias
				static const char *tevIndBiasAdd[] = { "int(-128)", "int(1)", "int(1)", "int(1)" }; // indexed by fmt
																																// bias
				if (bpm.tevind[n].bias == ITB_S || bpm.tevind[n].bias == ITB_T || bpm.tevind[n].bias == ITB_U)
					out.Write("indtevcrd%d.%s += %s;\n", n, tevIndBiasField[bpm.tevind[n].bias], tevIndBiasAdd[bpm.tevind[n].fmt]);
				else if (bpm.tevind[n].bias == ITB_ST || bpm.tevind[n].bias == ITB_SU || bpm.tevind[n].bias == ITB_TU)
					out.Write("indtevcrd%d.%s += int2(%s, %s);\n", n, tevIndBiasField[bpm.tevind[n].bias], tevIndBiasAdd[bpm.tevind[n].fmt], tevIndBiasAdd[bpm.tevind[n].fmt]);
				else if (bpm.tevind[n].bias == ITB_STU)
					out.Write("indtevcrd%d.%s += int3(%s, %s, %s);\n", n, tevIndBiasField[bpm.tevind[n].bias], tevIndBiasAdd[bpm.tevind[n].fmt], tevIndBiasAdd[bpm.tevind[n].fmt], tevIndBiasAdd[bpm.tevind[n].fmt]);

				// multiply by offset matrix and scale
				if (bpm.tevind[n].mid <= 3)
				{
					int mtxidx = 2 * (bpm.tevind[n].mid - 1);
					out.Write("int2 indtevtrans%d = int2(idot(" I_INDTEXMTX "[%d].xyz, indtevcrd%d), idot(" I_INDTEXMTX "[%d].xyz, indtevcrd%d));\n",
						n, mtxidx, n, mtxidx + 1, n);

					out.Write("indtevtrans%d = BSHR(indtevtrans%d, int(3));\n", n, n);
					out.Write("indtevtrans%d = BSH(indtevtrans%d, " I_INDTEXMTX "[%d].w);\n", n, n, mtxidx);
				}
				else if (bpm.tevind[n].mid <= 7 && bHasTexCoord)
				{ // s matrix
					_assert_(bpm.tevind[n].mid >= 5);
					int mtxidx = 2 * (bpm.tevind[n].mid - 5);
					out.Write("int2 indtevtrans%d = int2(uv[%d].xy * indtevcrd%d.xx);\n", n, texcoord, n);
					out.Write("indtevtrans%d = BSHR(indtevtrans%d, int(8));\n", n, n);
					out.Write("indtevtrans%d = BSH(indtevtrans%d, " I_INDTEXMTX "[%d].w);\n", n, n, mtxidx);
				}
				else if (bpm.tevind[n].mid <= 11 && bHasTexCoord)
				{ // t matrix
					_assert_(bpm.tevind[n].mid >= 9);
					int mtxidx = 2 * (bpm.tevind[n].mid - 9);
					out.Write("int2 indtevtrans%d = int2(uv[%d].xy * indtevcrd%d.yy);\n", n, texcoord, n);
					out.Write("indtevtrans%d = BSHR(indtevtrans%d, int(8));\n", n, n);
					out.Write("indtevtrans%d = BSH(indtevtrans%d, " I_INDTEXMTX "[%d].w);\n", n, n, mtxidx);
				}
				else
				{
					out.Write("int2 indtevtrans%d = int2(0,0);\n", n);
				}
			}
			else
			{
				out.Write("int2 indtevtrans%d = int2(0,0);\n", n);
			}
			// ---------
			// Wrapping
			// ---------
			static const char *tevIndWrapStart[] = { "int(0)", "int(256*128)", "int(128*128)", "int(64*128)", "int(32*128)", "int(16*128)", "int(1)" };
			// wrap S
			if (bpm.tevind[n].sw == ITW_OFF)
				out.Write("wrappedcoord.x = int(uv[%d].x);\n", texcoord);
			else if (bpm.tevind[n].sw == ITW_0)
				out.Write("wrappedcoord.x = int(0);\n");
			else
				out.Write("wrappedcoord.x = remainder(int(uv[%d].x), %s);\n", texcoord, tevIndWrapStart[bpm.tevind[n].sw]);

			// wrap T
			if (bpm.tevind[n].tw == ITW_OFF)
				out.Write("wrappedcoord.y = int(uv[%d].y);\n", texcoord);
			else if (bpm.tevind[n].tw == ITW_0)
				out.Write("wrappedcoord.y = int(0);\n");
			else
				out.Write("wrappedcoord.y = remainder(int(uv[%d].y), %s);\n", texcoord, tevIndWrapStart[bpm.tevind[n].tw]);

			if (bpm.tevind[n].fb_addprev) // add previous tevcoord
				out.Write("tevcoord.xy += wrappedcoord + indtevtrans%d;\n", n);
			else
				out.Write("tevcoord.xy = wrappedcoord + indtevtrans%d;\n", n);

			// Emulate s24 overflows
			out.Write("tevcoord.xy = (tevcoord.xy << 8) >> 8;\n");
		}
		else
		{
			// calc tevcord
			if (bHasTexCoord)
				out.Write("tevcoord.xy = int2(uv[%d].xy);\n", texcoord);
			else
				out.Write("tevcoord.xy = int2(0,0);\n");
		}

		out.Write("float2 stagecoord = float2(tevcoord.xy) * (1.0/128.0);\n");
		out.Write("float bump = ");
		SampleTextureRAW<T, ApiType>(out, "(stagecoord)", "b", "0.0", texmap, texcoord);
		out.Write("bump = (bump * 255.0/127.0 - 128.0/127.0) * uv[%d].z;\n", texcoord);
		out.Write("displacement = displacement * displacementcount + bump;\n");
		// finalize Running average
		out.Write("displacementcount+=1.0;");
		out.Write("displacement = displacement / displacementcount;\n");
		out.Write("}\n");
	}
	out.Write("}\n");
}

template<class T, API_TYPE ApiType, bool is_writing_shadercode>
static inline void GenerateHullDomainShader(T& out, const XFMemory& xfr, const BPMemory& bpm, const u32 components)
{
	// Non-uid template parameters will Write to the dummy data (=> gets optimized out)
	HullDomain_shader_uid_data dummy_data;
	bool uidPresent = (&out.template GetUidData<HullDomain_shader_uid_data>() != nullptr);
	HullDomain_shader_uid_data& uid_data = uidPresent ? out.template GetUidData<HullDomain_shader_uid_data>() : dummy_data;

	if (uidPresent)
	{
		out.ClearUID();
	}

	u32 numStages = bpm.genMode.numtevstages.Value() + 1;
	u32 numTexgen = xfr.numTexGen.numTexGens;
	u32 numindStages = bpm.genMode.numindstages.Value();
	bool normalpresent = (components & VB_HAS_NRM0) != 0;
	uid_data.numTexGens = numTexgen;
	uid_data.normal = normalpresent;
	uid_data.genMode_numtevstages = bpm.genMode.numtevstages;
	uid_data.genMode_numindstages = numindStages;
	int nIndirectStagesUsed = 0;
	if (numindStages > 0)
	{
		for (u32 i = 0; i < numStages; ++i)
		{
			if (bpm.tevind[i].IsActive() && bpm.tevind[i].bt < numindStages)
				nIndirectStagesUsed |= 1 << bpm.tevind[i].bt;
		}
	}
	uid_data.nIndirectStagesUsed = nIndirectStagesUsed;
	bool enable_pl = g_ActiveConfig.PixelLightingEnabled(xfr, components);
	bool enable_diffuse_ligthing = false;
	if (enable_pl)
	{
		for (u32 i = 0; i < xfr.numChan.numColorChans; i++)
		{
			const LitChannel& color = xfr.color[i];
			const LitChannel& alpha = xfr.alpha[i];
			if (color.enablelighting || alpha.enablelighting)
			{
				enable_diffuse_ligthing = true;
				break;
			}
		}
	}
	bool enablenormalmaps = enable_diffuse_ligthing && g_ActiveConfig.HiresMaterialMapsEnabled();
	if (enablenormalmaps)
	{
		enablenormalmaps = false;
		for (u32 i = 0; i < numStages; ++i)
		{
			if (bpm.tevorders[i / 2].getEnable(i & 1))
			{
				enablenormalmaps = true;
				break;
			}
		}
	}
	uid_data.pixel_normals = enablenormalmaps ? 1 : 0;
	if (enablenormalmaps)
	{
		for (u32 i = 0; i < numTexgen; ++i)
		{
			// optional perspective divides
			uid_data.texMtxInfo_n_projection |= xfr.texMtxInfo[i].projection << i;
		}
		for (u32 i = 0; i < numindStages; ++i)
		{
			if (nIndirectStagesUsed & (1 << i))
			{
				u32 texcoord = bpm.tevindref.getTexCoord(i);
				u32 texmap = bpm.tevindref.getTexMap(i);
				uid_data.SetTevindrefValues(i, texcoord, texmap);
			}
		}
		for (u32 i = 0; i < numStages; ++i)
		{
			WriteStageUID(uid_data, i, bpm); // Fetch Texture data
		}
	}
	char* codebuffer = nullptr;
	if (is_writing_shadercode)
	{
		codebuffer = out.GetBuffer();
		if (codebuffer == nullptr)
		{
			codebuffer = text;
			out.SetBuffer(codebuffer);
		}
		codebuffer[sizeof(text) - 1] = 0x7C;  // canary
	}
	else
	{
		return;
	}
	if (enablenormalmaps)
	{
		if (ApiType == API_OPENGL)
		{
			// Declare samplers
			out.Write("SAMPLER_BINDING(0) uniform sampler2DArray samp[16];\n");
		}
		else
		{
			out.Write("SamplerState samp[8] : register(s0);\n");
			out.Write("Texture2DArray Tex[16] : register(t0);\n");
		}
		out.Write(headerUtilI);
	}
	// uniforms
	if (ApiType == API_OPENGL)
		out.Write("layout(std140%s) uniform GSBlock {\n", g_ActiveConfig.backend_info.bSupportsBindingLayout ? ", binding = 3" : "");
	else
		out.Write("cbuffer GSBlock {\n");
	out.Write(
		"\tfloat4 " I_TESSPARAMS";\n"
		"\tfloat4 " I_DEPTHPARAMS";\n"
		"\tfloat4 " I_PROJECTION"[4];\n"
		"\tfloat4 " I_TEXDIMS"[8];\n"
		"\tint4 " I_INDTEXSCALE"[2];\n"
		"\tint4 " I_INDTEXMTX"[6];\n"
		"\tint4 " I_FLAGS";\n"
		"};\n");

	out.Write("struct VS_OUTPUT {\n");
	GenerateVSOutputMembers<T, ApiType>(out, normalpresent, xfr);
	out.Write("};\n");

	if (ApiType == API_OPENGL)
	{

	}
	else // D3D
	{
		out.Write("struct ConstantOutput\n"
			"{\n"
			"float EFactor[3] : SV_TessFactor;\n"
			"float InsideFactor : SV_InsideTessFactor;\n"
			"float4 edgesize : TEXCOORD1;\n"
			"float4 pos[3] : TEXCOORD2;\n"
			"float4 colors_0[3] : TEXCOORD5;\n"
			"float4 colors_1[3] : TEXCOORD8;\n");
		u32 texcount = xfr.numTexGen.numTexGens < 7 ? xfr.numTexGen.numTexGens : 8;
		for (unsigned int i = 0; i < texcount; ++i)
			out.Write("float4 tex%d[3] : TEXCOORD%d;\n", i, i * 3 + 11);

		if (xfr.numTexGen.numTexGens < 7 && normalpresent)
		{
			out.Write("float4 Normal[3]: TEXCOORD%d;\n", xfr.numTexGen.numTexGens * 3 + 11);
		}
		out.Write("};\n");
		out.Write(s_hlsl_header_str);
		out.Write("[unroll]\n"
			"for(int i = 0; i < 3; i++)\n{\n");
		if (xfr.numTexGen.numTexGens < 7)
		{
			out.Write("result.pos[i] = float4(patch[i].clipPos.x,patch[i].clipPos.y,patch[i].Normal.w, 1.0);\n");
		}
		else
		{
			out.Write("result.pos[i] = float4(patch[i].tex0.w, patch[i].tex1.w, patch[i].tex7.w, 1.0);\n");
		}
		out.Write("result.colors_0[i] = patch[i].colors_0;\n"
			"result.colors_1[i] = patch[i].colors_1;\n");
		for (u32 i = 0; i < texcount; ++i)
		{
			out.Write("result.tex%d[i] = float4(patch[i].tex%d, 1.0);\n", i, i);
			if (xfr.texMtxInfo[i].projection == XF_TEXPROJ_STQ)
			{
				out.Write("{\n");
				out.Write("float2 t0 = patch[i].tex%d.xy;", i);
				out.Write("if (patch[i].tex%d.z != 0.0) t0 = t0 /patch[i].tex%d.z;", i, i);				
				out.Write("float2 t1 = patch[(i + 1) %% 3].tex%d.xy;", i);
				out.Write("if (patch[(i + 1) %% 3].tex%d.z != 0.0) t0 = t0 /patch[(i + 1) %% 3].tex%d.z;", i, i);
				out.Write("result.tex%d[i].w = distance(t0, t1);\n", i);
				out.Write("}\n");
			}
			else
			{
				out.Write("{\n");
				out.Write("float2 t0 = patch[i].tex%d.xy;", i);
				out.Write("float2 t1 = patch[(i + 1) %% 3].tex%d.xy;", i);
				out.Write("result.tex%d[i].w = distance(t0, t1);\n", i);
				out.Write("}\n");				
			}
		}
		if (xfr.numTexGen.numTexGens < 7 && normalpresent)
		{
			out.Write("result.Normal[i] = patch[i].Normal;\n");
		}
		out.Write("}\n");

		out.Write(
			"float l0 = distance(result.pos[1].xyz,result.pos[2].xyz);\n"
			"float l1 = distance(result.pos[2].xyz,result.pos[0].xyz);\n"
			"float l2 = distance(result.pos[0].xyz,result.pos[1].xyz);\n"
			"result.edgesize = float4(l0, l1, l2, 1.0);\n"
			"result.EFactor[0] = CalcTessFactor((result.pos[1].xyz+result.pos[2].xyz) * 0.5, l0);\n"
			"result.EFactor[1] = CalcTessFactor((result.pos[2].xyz+result.pos[0].xyz) * 0.5, l1);\n"
			"result.EFactor[2] = CalcTessFactor((result.pos[0].xyz+result.pos[1].xyz) * 0.5, l2);\n"
			"result.InsideFactor = (result.EFactor[0] + result.EFactor[1] + result.EFactor[2]) / 3;\n"
			"return result;\n};\n"
			);
		out.Write(s_hlsl_ds_str);

		for (u32 i = 0; i < texcount; ++i)
			out.Write("result.tex%d = BInterpolate(pconstans.tex%d, bCoords).xyz;\n", i, i, i, i);

		out.Write("float displacement = 0.0, displacementcount = 0.0;\n");
		out.Write("int3 tevcoord=int3(0,0,0);\n");
		out.Write("int2 wrappedcoord = int2(0, 0);\n");
		if (enablenormalmaps)
		{
			out.Write("if(" I_FLAGS ".x != 0)\n{\n");
			out.Write("float4 uv[%d];\n", numTexgen);
			out.Write("int2 t_coord;\n");
			for (u32 i = 0; i < numTexgen; ++i)
			{
				if (xfr.texMtxInfo[i].projection == XF_TEXPROJ_STQ)
				{
					out.Write("if (result.tex%d.z != 0.0)", i);
					out.Write("\tuv[%d].xy = result.tex%d.xy / result.tex%d.z;\n", i, i, i);
				}
				else
				{
					out.Write("uv[%d].xy = result.tex%d.xy;\n", i, i);
				}
				out.Write("uv[%d].xy = trunc(128.0 * uv[%d].xy * " I_TEXDIMS"[%d].zw);\n", i, i, i);
				out.Write("uv[%d].z = dot(pconstans.edgesize.zxy, bCoords)/dot(float3(pconstans.tex%d[0].w, pconstans.tex%d[1].w, pconstans.tex%d[2].w), bCoords);\n", i, i, i, i);
				out.Write("uv[%d].w = dot(log2(4.0*float3(pconstans.tex%d[0].w,pconstans.tex%d[1].w,pconstans.tex%d[2].w) / float3(pconstans.EFactor[2], pconstans.EFactor[0], pconstans.EFactor[1])),bCoords);\n", i, i, i, i);
				out.Write("uv[%d].z = all(bCoords) ?  uv[%d].z : 1.0;\n", i, i);
				out.Write("uv[%d].w = all(bCoords) ?  uv[%d].w : 0.5;\n", i, i);
			}
			for (u32 i = 0; i < numindStages; ++i)
			{
				if (nIndirectStagesUsed & (1 << i))
				{
					u32 texcoord = bpm.tevindref.getTexCoord(i);
					u32 texmap = bpm.tevindref.getTexMap(i);
					if (texcoord < numTexgen)
					{
						out.Write("t_coord = BSHR(int2(uv[%d].xy) , " I_INDTEXSCALE"[%d].%s);\n", texcoord, i / 2, (i & 1) ? "zw" : "xy");
					}
					else
					{
						out.Write("t_coord = int2(0,0);\n");
					}
					out.Write("int3 indtex%d = ", i);
					SampleTexture<T, ApiType>(out, "(float2(t_coord)/128.0)", "abg", texmap);
				}
			}
			for (u32 i = 0; i < numStages; ++i)
			{
				WriteFetchDisplacement<T, ApiType>(out, i, bpm); // Fetch Texture data
			}
			out.Write("}\n");
		}

		out.Write("float3 pos0 = pconstans.pos[0].xyz;\n"
			"float3 pos1 = pconstans.pos[1].xyz;\n"
			"float3 pos2 = pconstans.pos[2].xyz;\n"
			"float3 position = BInterpolate(pos0, pos1, pos2, bCoords);\n");

		if (normalpresent)
		{
			if (xfr.numTexGen.numTexGens < 7)
			{
				out.Write(
					"float3 norm0 = pconstans.Normal[0].xyz;\n"
					"float3 norm1 = pconstans.Normal[1].xyz;\n"
					"float3 norm2 = pconstans.Normal[2].xyz;\n");
			}
			else
			{
				out.Write("float3 norm0 = float3(pconstans.tex4[0].w, pconstans.tex5[0].w, pconstans.tex6[0].w);\n"
					"float3 norm1 = float3(pconstans.tex4[1].w, pconstans.tex5[1].w, pconstans.tex6[1].w);\n"
					"float3 norm2 = float3(pconstans.tex4[2].w, pconstans.tex5[2].w, pconstans.tex6[2].w);\n");
			}
			out.Write("float3 normal = BInterpolate(norm0, norm1, norm2, bCoords);\n"
				"pos0 = PrjToPlane(norm0, pos0, position);\n"
				"pos1 = PrjToPlane(norm1, pos1, position);\n"
				"pos2 = PrjToPlane(norm2, pos2, position);\n"
				"position = lerp(position, BInterpolate(pos0, pos1, pos2, bCoords)," I_TESSPARAMS ".zzz);\n"
				"position += displacement * normal * 0.0625 * " I_TESSPARAMS ".w;\n");
		}
		// Transform world position to view-projection
		out.Write("float4 pos = float4(position, 1.0);\n"
			"result.pos = float4(dot(" I_PROJECTION "[0], pos), dot(" I_PROJECTION "[1], pos), dot(" I_PROJECTION "[2], pos), dot(" I_PROJECTION "[3], pos));\n"
			"result.pos.z = -((" I_DEPTHPARAMS".x - 1.0) * result.pos.w + result.pos.z * " I_DEPTHPARAMS".y);\n"
			"result.pos.xy = result.pos.xy + result.pos.w * " I_DEPTHPARAMS".zw;\n"
			"result.colors_0 = BInterpolate(pconstans.colors_0, bCoords);\n"
			"result.colors_1 = BInterpolate(pconstans.colors_1, bCoords);\n");
		if (xfr.numTexGen.numTexGens < 7)
		{
			out.Write("result.clipPos = float4(position.xy, result.pos.zw);\n");
			if (normalpresent)
			{
				out.Write("result.Normal = float4(normal.xyz, position.z);\n");
			}
		}
		else
		{
			// Store clip position in the w component of first 4 texcoords
			out.Write("result.tex0.w = position.x;\n"
				"result.tex1.w = position.y;\n");
			if (normalpresent)
			{
				out.Write("result.tex4.w = normal.x;\n"
					"result.tex5.w = normal.y;\n"
					"result.tex6.w = normal.z;\n");
			}

			if (xfr.numTexGen.numTexGens < 8)
				out.Write("result.tex7 = position.xyzz;\n");
			else
				out.Write("result.tex7.w = position.z;\n");
		}
		out.Write("return result;\n}");
	}

	if (is_writing_shadercode)
	{
		if (codebuffer[HULLDOMAINSHADERGEN_BUFFERSIZE - 1] != 0x7C)
			PanicAlert("GeometryShader generator - buffer too small, canary has been eaten!");
	}
	if (uidPresent)
	{
		out.CalculateUIDHash();
	}
}

void GenerateHullDomainShaderCode(ShaderCode& object, API_TYPE ApiType, const XFMemory &xfr, const BPMemory &bpm, const u32 components)
{
	if (ApiType == API_OPENGL)
	{
		GenerateHullDomainShader<ShaderCode, API_OPENGL, true>(object, xfr, bpm, components);
	}
	else
	{
		GenerateHullDomainShader<ShaderCode, API_D3D11, true>(object, xfr, bpm, components);
	}
}

void GetHullDomainShaderUid(HullDomainShaderUid& object, API_TYPE ApiType, const XFMemory &xfr, const BPMemory &bpm, const u32 components)
{
	if (ApiType == API_OPENGL)
	{
		GenerateHullDomainShader<HullDomainShaderUid, API_OPENGL, false>(object, xfr, bpm, components);
	}
	else
	{
		GenerateHullDomainShader<HullDomainShaderUid, API_D3D11, false>(object, xfr, bpm, components);
	}
}
