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

    uniform mat4 glH_ObjViewMatrix;
    uniform mat4 glH_ObjectMatrix;
    uniform mat4 glH_ViewMatrix;
    uniform mat4 glH_ProjectMatrix;
    uniform vec2 glH_ScreenSize;

    out parms
    {
        vec3  color;
    } vsOut;


    vec2 CalculateQuadPos(int GSplatVtxIdx)
    {
        vec2 quadPos = vec2(0,0);
        if (GSplatVtxIdx == 1 || GSplatVtxIdx == 2)
        {
            quadPos = vec2(1,0);
        }
        else
        if (GSplatVtxIdx == 3 || GSplatVtxIdx == 4)
        {
            quadPos = vec2(1,1);
        }
        else
        if (GSplatVtxIdx == 5 || GSplatVtxIdx == 6)
        {
            quadPos = vec2(0,1);
        }
        quadPos = (quadPos * 2) - 1;
        quadPos *= 2;
        return quadPos;
    }


    void main() {
        int GSplatVtxIdx = gl_VertexID % 8;
        vec2 quadPos = CalculateQuadPos(GSplatVtxIdx);

        mat4 flipYMatrix = mat4(1,0,0,0,0,-1,0,0,0,0,1,0,0,0,0,1);

        vec4 centerViewPos  = (glH_ObjViewMatrix * vec4(P, 1.0));
        vec4 centerClipPos  = (glH_ProjectMatrix * flipYMatrix) * centerViewPos;
                
        mat3 splatRotScaleMat = CalcMatrixFromRotationScale(orient.wxyz, scale);
        splatRotScaleMat = splatRotScaleMat * transpose(mat3(glH_ObjectMatrix));

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
    uniform int GSplatCount;
    uniform int GSplatVertexCount;
    uniform int GSplatZOrderTexDim;
    uniform isampler2D GSplatZOrderIntegerTexSampler;
    uniform int GSplatPosColorAlphaScaleOrientTexDim;
    uniform sampler2D GSplatPosColorAlphaScaleOrientTexSampler;
    
    uniform int GSplatShDeg1And2TexDim;
    uniform sampler2D GSplatShDeg1And2TexSampler;
    uniform int GSplatShDeg3TexDim;
    uniform sampler2D GSplatShDeg3TexSampler;
    
    uniform int GSplatShOrder;
    uniform vec3 GSplatOrigin;
    

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

    uniform mat4    glH_ObjViewMatrix;
    uniform mat4    glH_ObjectMatrix;
    uniform mat4	glH_InvObjectMatrix;
    uniform mat4    glH_ViewMatrix;
    uniform mat4	glH_ProjectMatrix;
    uniform vec2	glH_DepthRange;
    uniform vec2	glH_ScreenSize;

    ivec2 computeTextureCoordinates(int index, int textureDimension, int pixelStride) {
        int linearIndex = index * pixelStride;
        int row = linearIndex / textureDimension;
        int column = linearIndex % textureDimension;
        return ivec2(column, row);
    }

    vec2 CalculateQuadPos(int GSplatVtxIdx)
    {
        vec2 quadPos = vec2(0,0);
        if (GSplatVtxIdx == 0)
        {
            quadPos = vec2(1,0);
        }
        else
        if (GSplatVtxIdx == 3)
        {
            quadPos = vec2(0,1);
        }
        else
        if (GSplatVtxIdx == 1 || GSplatVtxIdx == 5)
        {
            quadPos = vec2(1,1);
        }
        quadPos = (quadPos * 2) - 1;
        quadPos *= 2;
        return quadPos;
    }

    void main()
    {
        int GsplatIdx = gl_InstanceID;
        int GSplatVtxIdx = gl_VertexID;

        ivec2 iuv;
        
        iuv = computeTextureCoordinates(GsplatIdx, GSplatZOrderTexDim, 1);
        GsplatIdx = texelFetch(GSplatZOrderIntegerTexSampler, iuv, 0).r;

        iuv = computeTextureCoordinates(GsplatIdx, GSplatPosColorAlphaScaleOrientTexDim, 4);
        vec3 P = texelFetch(GSplatPosColorAlphaScaleOrientTexSampler, iuv, 0).rgb;
        P += GSplatOrigin;

        mat4 flipYMatrix = mat4(1,0,0,0,0,-1,0,0,0,0,1,0,0,0,0,1);
        
        vec3 centerViewPos = (glH_ObjViewMatrix * vec4(P, 1.0)).xyz;
        vec4 centerClipPos = ((glH_ProjectMatrix * flipYMatrix) * vec4(centerViewPos, 1));

        if (centerClipPos.w <= 0)
        {
            // set behind camera to fully transparent so is discarded in fragment shader
            gl_Position = vec4(0,0,0,0);
            vsOut.opacity = 0.0;
        }
        else
        {
            // Unpack color, alpha, scale, orient 
            vec4 color_and_alpha = texelFetch(GSplatPosColorAlphaScaleOrientTexSampler, iuv + ivec2(1, 0), 0).rgba;
            vec3 color = color_and_alpha.rgb;
            float alpha = color_and_alpha.a;
            vec3 scale = texelFetch(GSplatPosColorAlphaScaleOrientTexSampler, iuv + ivec2(2, 0), 0).rgb;
            vec4 orient = texelFetch(GSplatPosColorAlphaScaleOrientTexSampler, iuv + ivec2(3, 0), 0).xyzw;

            vsOut.color = color;
            vsOut.opacity = alpha;

            vec2 quadPos = CalculateQuadPos(GSplatVtxIdx);
            vsOut.pos = vec4(quadPos, 0, 1);
            
            mat3 splatRotScaleMat = CalcMatrixFromRotationScale(orient.wxyz, scale);
            splatRotScaleMat = splatRotScaleMat * transpose(mat3(glH_ObjectMatrix));

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

            if (GSplatShOrder > 0)
            {
                vec3 sh1, sh2, sh3, sh4, sh5, sh6, sh7, sh8, sh9, sh10, sh11, sh12, sh13, sh14, sh15;

                // Unpack spherical harmonics
                iuv = computeTextureCoordinates(GsplatIdx, GSplatShDeg1And2TexDim, 8);
                sh1 = texelFetch(GSplatShDeg1And2TexSampler, iuv, 0).rgb;
                sh2 = texelFetch(GSplatShDeg1And2TexSampler, iuv + ivec2(1,0), 0).rgb;
                sh3 = texelFetch(GSplatShDeg1And2TexSampler, iuv + ivec2(2,0), 0).rgb;
                sh4 = texelFetch(GSplatShDeg1And2TexSampler, iuv + ivec2(3,0), 0).rgb;
                sh5 = texelFetch(GSplatShDeg1And2TexSampler, iuv + ivec2(4,0), 0).rgb;
                sh6 = texelFetch(GSplatShDeg1And2TexSampler, iuv + ivec2(5,0), 0).rgb;
                sh7 = texelFetch(GSplatShDeg1And2TexSampler, iuv + ivec2(6,0), 0).rgb;
                sh8 = texelFetch(GSplatShDeg1And2TexSampler, iuv + ivec2(7,0), 0).rgb;
                
                if (GSplatShOrder > 2)
                {
                    iuv = computeTextureCoordinates(GsplatIdx, GSplatShDeg3TexDim, 8);
                    sh9  = texelFetch(GSplatShDeg3TexSampler, iuv, 0).rgb;
                    sh10 = texelFetch(GSplatShDeg3TexSampler, iuv + ivec2(1,0), 0).rgb;
                    sh11 = texelFetch(GSplatShDeg3TexSampler, iuv + ivec2(2,0), 0).rgb;
                    sh12 = texelFetch(GSplatShDeg3TexSampler, iuv + ivec2(3,0), 0).rgb;
                    sh13 = texelFetch(GSplatShDeg3TexSampler, iuv + ivec2(4,0), 0).rgb;
                    sh14 = texelFetch(GSplatShDeg3TexSampler, iuv + ivec2(5,0), 0).rgb;
                    sh15 = texelFetch(GSplatShDeg3TexSampler, iuv + ivec2(6,0), 0).rgb;
                }
                
                // Flip Z to convert to HLSL CS, as expected by ShadeSH
                vec3 worldViewDir = vec3(WorldSpaceCameraPos.x, WorldSpaceCameraPos.y, -WorldSpaceCameraPos.z) - vec3(P.x, P.y, -P.z); 
                vec3 objViewDir = mat3(glH_InvObjectMatrix) * worldViewDir;
                objViewDir = normalize(objViewDir);
                vsOut.color = ShadeSH(vsOut.color, sh1, sh2, sh3, sh4, sh5, sh6, sh7, sh8, sh9, sh10, sh11, sh12, sh13, sh14, sh15, objViewDir, GSplatShOrder, false);
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
