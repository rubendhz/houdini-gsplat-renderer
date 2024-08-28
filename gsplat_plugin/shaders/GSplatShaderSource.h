#ifndef __GSPLAT_SHADER_SOURCE__
#define __GSPLAT_SHADER_SOURCE__


#include "GSplatShaderCoreLib.h"
#include <string>


std::string getFullShaderSrc(const char* version, const std::initializer_list<const char*>& parts) {
    std::string shader = std::string("#version ") + version + "\n";
    for (const char* part : parts) {
        shader += part;
    }
    return shader;
}


//
// Wireframe Shader
//

const char* const _GSplatWireVertexShader = R"glsl(
    
    in vec3 P;
    in vec3 Cd;
    in vec3 scale;
    in vec4 orient;
    //in float gSplatVertIdxNorm;

    uniform mat4 glH_ObjViewMatrix;
    uniform mat4 glH_ViewMatrix;
    uniform mat4 glH_ProjectMatrix;
    uniform vec2 glH_ScreenSize;

    out parms
    {
        vec3  color;
    } vsOut;


    vec2 CalculateQuadPos(int gSplatVtxIdx)
    {
        vec2 quadPos = vec2(0,0);
        if (gSplatVtxIdx == 1 || gSplatVtxIdx == 2)
        {
            quadPos = vec2(1,0);
        }
        else
        if (gSplatVtxIdx == 3 || gSplatVtxIdx == 4)
        {
            quadPos = vec2(1,1);
        }
        else
        if (gSplatVtxIdx == 5 || gSplatVtxIdx == 6)
        {
            quadPos = vec2(0,1);
        }
        quadPos = (quadPos * 2) - 1;
        quadPos *= 2;
        return quadPos;
    }


    void main() {
        vec3 centerWorldPos = (glH_ObjViewMatrix * vec4(P, 1.0)).xyz;
        //vec4 centerClipPos = (glH_ProjectMatrix * vec4(centerWorldPos, 1));
        vec4 centerClipPos = ((glH_ProjectMatrix*mat4(1,0,0,0,0,-1,0,0,0,0,1,0,0,0,0,1)) * vec4(centerWorldPos, 1));
        
        int gSplatVtxIdx = gl_VertexID % 8;
        
        vec2 quadPos = CalculateQuadPos(gSplatVtxIdx);
                
        vec4 _orient = orient.wxyz;

        mat3 splatRotScaleMat = CalcMatrixFromRotationScale(_orient, scale);
        vec3 cov3d0, cov3d1;
        mat3 sigma;
        CalcCovariance3D(splatRotScaleMat, cov3d0, cov3d1, sigma);
        vec3 cov2d = CalcCovariance2D(P, cov3d0, cov3d1, glH_ViewMatrix, glH_ProjectMatrix, glH_ScreenSize, sigma);
        vec2 view_axis1, view_axis2;
        DecomposeCovariance(cov2d, view_axis1, view_axis2);

        vec2 deltaScreenPos = (quadPos.x * view_axis1 + quadPos.y * view_axis2) * 2 / glH_ScreenSize;
        vec4 out_vertex = centerClipPos;
        out_vertex.xy += deltaScreenPos * centerClipPos.w;
        out_vertex.y = -out_vertex.y;

        gl_Position = out_vertex;
        vsOut.color = Cd;
    }

)glsl";
const std::string GSplatWireVertexShader = getFullShaderSrc("330", {GSplatCoreLib, _GSplatWireVertexShader});


const char* const _GSplatWireFragmentShader = R"glsl(
    
    in parms
    {
        vec3 color;
    } fsIn;
    
    out vec4 color_out;


    void main() {
        color_out = vec4(fsIn.color, 1.0);
    }

)glsl";
const std::string GSplatWireFragmentShader = getFullShaderSrc("330", {_GSplatWireFragmentShader});


//
// Main Shader
//

const char* const _GSplatMainVertexShader = R"glsl(
    
    uniform vec3 WorldSpaceCameraPos;
    uniform int gSplatCount;
    uniform int gSplatVertexCount;
    uniform int gSplatZOrderTexDim;
    uniform sampler2D gSplatZOrderTexSampler;
    uniform int gSplatShTexDim;
    uniform sampler2D gSplatShTexSampler;
    uniform int gSplatColorAlphaScaleOrientTexDim;
    uniform sampler2D gSplatColorAlphaScaleOrientTexSampler;
    uniform int gSplatShEnabled;
    uniform vec3 gSplatOrigin;

    out parms
    {
        vec4  pos;
        vec3  color;
        float opacity;
    } vsOut;

    #if defined(VENDOR_NVIDIA) && DRIVER_MAJOR >= 343
    out gl_PerVertex
    {
        vec4 gl_Position;
    #ifdef CLIP_DISTANCE
        float gl_ClipDistance[2];
    #endif // CLIP_DISTANCE
    };
    #endif // defined(VENDOR_NVIDIA)...

    uniform mat4	glH_ObjViewMatrix;
    uniform mat4    glH_ViewMatrix;
    uniform mat4	glH_ProjectMatrix;
    uniform vec2	glH_DepthRange;
    uniform vec2	glH_ScreenSize;
    uniform mat4	glH_InvObjectMatrix;

    void get_uv(float idx_norm, int tex_dim, int n, int pxl_stride, out vec2 uv, out vec2 uv_inc) {
        int pxl_idx = int((idx_norm*(n*pxl_stride))+0.5);
        int pxl_row = int(float(pxl_idx + 0.5)/tex_dim);
        int pxl_column = pxl_idx - (pxl_row * tex_dim);
        float pxl_width = 1.0 / float(tex_dim);
        float pxl_half_width = 0.5 * pxl_width;
        uv = vec2(pxl_column * pxl_width + pxl_half_width, pxl_row * pxl_width + pxl_half_width);
        uv_inc = vec2(pxl_width, 0.0);
    }

    vec2 CalculateQuadPos(int gSplatVtxIdx)
    {
        vec2 quadPos = vec2(0,0);
        if (gSplatVtxIdx == 0)
        {
            quadPos = vec2(1,0);
        }
        else
        if (gSplatVtxIdx == 3)
        {
            quadPos = vec2(0,1);
        }
        else
        if (gSplatVtxIdx == 1 || gSplatVtxIdx == 5)
        {
            quadPos = vec2(1,1);
        }
        quadPos = (quadPos * 2) - 1;
        quadPos *= 2;
        return quadPos;
    }

    void main()
    {
        //int vtx_id = gl_VertexID; // if not instanced
        int vtx_id = gl_InstanceID * gSplatVertexCount + gl_VertexID;

        float gSplatIdxNorm = float(int(vtx_id / gSplatVertexCount)) / gSplatCount;
        float gSplatVertIdxNorm = (vtx_id % gSplatVertexCount) / float(gSplatVertexCount);

        vec2 uv, uv_inc;
        get_uv(gSplatIdxNorm, gSplatZOrderTexDim, gSplatCount, 1, uv, uv_inc);
        gSplatIdxNorm = texture(gSplatZOrderTexSampler, uv).r;

        get_uv(gSplatIdxNorm, gSplatColorAlphaScaleOrientTexDim, gSplatCount, 4, uv, uv_inc);
        vec3 P = texture(gSplatColorAlphaScaleOrientTexSampler, uv).rgb;
        P += gSplatOrigin;
        
        vec3 centerWorldPos = (glH_ObjViewMatrix * vec4(P, 1.0)).xyz;
        vec4 centerClipPos = ((glH_ProjectMatrix*mat4(1,0,0,0,0,-1,0,0,0,0,1,0,0,0,0,1)) * vec4(centerWorldPos, 1));

        if (centerClipPos.w <= 0)
        {
            // set behind camera to fully transparent so is discarded in fragment shader
            gl_Position = vec4(0,0,0,0);
            vsOut.opacity = 0.0;
        }
        else
        {
            // UNPACK COLOR, ALPHA, SCALE, ORIENT ------------------------------------------------
            vec4 color_and_alpha = texture(gSplatColorAlphaScaleOrientTexSampler, uv + uv_inc).rgba;
            vec3 color = color_and_alpha.rgb;
            float alpha = color_and_alpha.a;
            vec3 scale = texture(gSplatColorAlphaScaleOrientTexSampler, uv + 2*uv_inc).rgb;
            vec4 orient = texture(gSplatColorAlphaScaleOrientTexSampler, uv + 3*uv_inc).xyzw;
            // -----------------------------------------------------------------------------------

            vec4 _orient = orient.wxyz;

            vsOut.color = color;
            vsOut.opacity = alpha;

            int gSplatVtxIdx = int((gSplatVertIdxNorm * 6) + 0.5);
            vec2 quadPos = CalculateQuadPos(gSplatVtxIdx);
            vsOut.pos = vec4(quadPos, 0, 1);
            
            mat3 splatRotScaleMat = CalcMatrixFromRotationScale(_orient, scale);
            vec3 cov3d0, cov3d1;
            mat3 sigma;
            CalcCovariance3D(splatRotScaleMat, cov3d0, cov3d1, sigma);
            float splatScale = 1.0;
            float splatScale2 = splatScale * splatScale;
            cov3d0 *= splatScale2;
            cov3d1 *= splatScale2;
            vec3 cov2d = CalcCovariance2D(P, cov3d0, cov3d1, glH_ViewMatrix, glH_ProjectMatrix, glH_ScreenSize, sigma);
            vec2 view_axis1, view_axis2;
            DecomposeCovariance(cov2d, view_axis1, view_axis2);

            if (gSplatShEnabled == 1)
            {
                // UNPACK SPHERICAL HARMONICS --------------------------------------
                get_uv(gSplatIdxNorm, gSplatShTexDim, gSplatCount, 16, uv, uv_inc);
                vec3 sh1 = texture(gSplatShTexSampler, uv).rgb;
                vec3 sh2 = texture(gSplatShTexSampler, uv + uv_inc).rgb;
                vec3 sh3 = texture(gSplatShTexSampler, uv + 2*uv_inc).rgb;
                vec3 sh4 = texture(gSplatShTexSampler, uv + 3*uv_inc).rgb;
                vec3 sh5 = texture(gSplatShTexSampler, uv + 4*uv_inc).rgb;
                vec3 sh6 = texture(gSplatShTexSampler, uv + 5*uv_inc).rgb;
                vec3 sh7 = texture(gSplatShTexSampler, uv + 6*uv_inc).rgb;
                vec3 sh8 = texture(gSplatShTexSampler, uv + 7*uv_inc).rgb;
                vec3 sh9 = texture(gSplatShTexSampler, uv + 8*uv_inc).rgb;
                vec3 shA = texture(gSplatShTexSampler, uv + 9*uv_inc).rgb;
                vec3 shB = texture(gSplatShTexSampler, uv + 10*uv_inc).rgb;
                vec3 shC = texture(gSplatShTexSampler, uv + 11*uv_inc).rgb;
                vec3 shD = texture(gSplatShTexSampler, uv + 12*uv_inc).rgb;
                vec3 shE = texture(gSplatShTexSampler, uv + 13*uv_inc).rgb;
                vec3 shF = texture(gSplatShTexSampler, uv + 14*uv_inc).rgb;
                // ----------------------------------------------------------------

                vec3 worldViewDir = WorldSpaceCameraPos - centerWorldPos;
                vec3 objViewDir = mat3(glH_InvObjectMatrix) * worldViewDir;
                objViewDir = normalize(objViewDir);
                vsOut.color = ShadeSH(vsOut.color, sh1, sh2, sh3, sh4, sh5, sh6, sh7, sh8, sh9, shA, shB, shC, shD, shE, shF, objViewDir, 3, false);
            }

            vec2 deltaScreenPos = (quadPos.x * view_axis1 + quadPos.y * view_axis2) * 2 / glH_ScreenSize;
            vec4 out_vertex = centerClipPos;
            out_vertex.xy += deltaScreenPos * centerClipPos.w;
            
            out_vertex.y = -out_vertex.y;
            gl_Position = out_vertex;
    #ifdef  CLIP_DISTANCE
            gl_ClipDistance[0] = -vsOut.pos.z - glH_DepthRange.x;
            gl_ClipDistance[1] = glH_DepthRange.y +vsOut.pos.z;
    #endif // CLIP_DISTANCE
        }
    }

)glsl";
const std::string GSplatMainVertexShader = getFullShaderSrc("330", {GSplatCoreLib, GSplatSphericalHarmonicsLib, _GSplatMainVertexShader});

const char* const _GSplatMainFragmentShader = R"glsl(
    
    in parms
    {
        vec4 pos;
        vec3 color;
        float opacity;
    } fsIn;

    out vec4 color_out;

    void main()
    {
        if (fsIn.opacity < 0.05)
            discard;
        float power = -dot(fsIn.pos.xy, fsIn.pos.xy);
        float alpha = exp(power);
        alpha = clamp(alpha * fsIn.opacity, 0.0, 1.0);
        if (alpha < 1.0/255.0)
            discard;
        color_out = vec4(fsIn.color * alpha, alpha);
    }

)glsl";
const std::string GSplatMainFragmentShader = getFullShaderSrc("330", {_GSplatMainFragmentShader});

#endif // __GSPLAT_SHADER_SOURCE__
