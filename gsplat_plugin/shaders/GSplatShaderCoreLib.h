#ifndef __GSPLAT_SHADER_CORE_LIB__
#define __GSPLAT_SHADER_CORE_LIB__

//
// Gaussian splatting Core functions
//

const char* const GSplatCoreLib = R"glsl(
    
    mat3 CalcMatrixFromRotationScale(vec4 rot, vec3 scale)
    {
        mat3 ms = mat3(
            scale.x, 0, 0,
            0, scale.y, 0,
            0, 0, scale.z
        );
        float x = rot.x;
        float y = rot.y;
        float z = rot.z;
        float w = rot.w;
        mat3 mr = mat3(
            1.0 - 2.0 * (rot.z * rot.z + rot.w * rot.w), 2.0 * (rot.y * rot.z - rot.x * rot.w), 2.0 * (rot.y * rot.w + rot.x * rot.z),
            2.0 * (rot.y * rot.z + rot.x * rot.w), 1.0 - 2.0 * (rot.y * rot.y + rot.w * rot.w), 2.0 * (rot.z * rot.w - rot.x * rot.y),
            2.0 * (rot.y * rot.w - rot.x * rot.z), 2.0 * (rot.z * rot.w + rot.x * rot.y), 1.0 - 2.0 * (rot.y * rot.y + rot.z * rot.z)
        );
        return ms * mr;
    }

    void CalcCovariance3D(mat3 rotMat, out vec3 sigma0, out vec3 sigma1, out mat3 sigma)
    {
        mat3 sig = transpose(rotMat) * rotMat;
        sigma = sig;
        sigma0 = vec3(sig[0][0], sig[0][1], sig[0][2]);
        sigma1 = vec3(sig[1][1], sig[1][2], sig[2][2]);
    }

    // from "EWA Splatting" (Zwicker et al 2002) eq. 31
    vec3 CalcCovariance2D(vec3 worldPos, vec3 cov3d0, vec3 cov3d1, mat4 matrixV, mat4 matrixP, vec2 screenSize, mat3 sigma)
    {
        mat4 viewMatrix = matrixV;
        vec3 viewPos = (viewMatrix * vec4(worldPos, 1)).xyz;

        // this is needed in order for splats that are visible in view but clipped "quite a lot" to work
        float aspect = matrixP[0][0] / matrixP[1][1];
        float tanFovX = 1.0 / (matrixP[0][0]);
        float tanFovY = 1.0 / (matrixP[1][1] * aspect);
        
        float limX = 1.3 * tanFovX;
        float limY = 1.3 * tanFovY;
        viewPos.x = clamp(viewPos.x / viewPos.z, -limX, limX) * viewPos.z;
        viewPos.y = clamp(viewPos.y / viewPos.z, -limY, limY) * viewPos.z;
        
        float focal = screenSize.x * matrixP[0][0] / 2;

        mat3 J = mat3(
            focal / viewPos.z, 0, -(focal * viewPos.x) / (viewPos.z * viewPos.z),
            0, focal / viewPos.z, -(focal * viewPos.y) / (viewPos.z * viewPos.z),
            0, 0, 0
        );
        mat3 W = mat3(viewMatrix);
        W = transpose(W);
        mat3 T = W * J;
        
        mat3 V = mat3(
            cov3d0.x, cov3d0.y, cov3d0.z,
            cov3d0.y, cov3d1.x, cov3d1.y,
            cov3d0.z, cov3d1.y, cov3d1.z
        );
        
        mat3 cov = transpose(T) * (transpose(sigma) * T);

        // Low pass filter to make each splat at least 1px size.
        cov[0][0] += 0.3;
        cov[1][1] += 0.3;
        return vec3(cov[0][0], cov[0][1], cov[1][1]);
    }

    // From antimatter15/splat
    void DecomposeCovariance(vec3 cov2d, out vec2 v1, out vec2 v2)
    {
        float diag1 = cov2d.x;
        float diag2 = cov2d.z;
        float offDiag = cov2d.y;
        float mid = 0.5 * (diag1 + diag2);
        float radius = length(vec2((diag1 - diag2) / 2.0, offDiag));
        float lambda1 = mid + radius;
        float lambda2 = max(mid - radius, 0.1);
        vec2 diagVec = normalize(vec2(offDiag, lambda1 - diag1));
        diagVec.y = -diagVec.y;
        v1 = sqrt(2.0 * lambda1) * diagVec;
        v2 = sqrt(2.0 * lambda2) * vec2(diagVec.y, -diagVec.x);
    }

)glsl";

//
// Gaussian Splatting Spherical Harmonics functions
//

const char* const GSplatSphericalHarmonicsLib = R"glsl(

    const float SH_C1 = 0.4886025;
    const float SH_C2_0 = 1.0925484;
    const float SH_C2_1 = -1.0925484;
    const float SH_C2_2 = 0.3153916;
    const float SH_C2_3 = -1.0925484;
    const float SH_C2_4 = 0.5462742;
    const float SH_C3_0 = -0.5900436;
    const float SH_C3_1 = 2.8906114;
    const float SH_C3_2 = -0.4570458;
    const float SH_C3_3 = 0.3731763;
    const float SH_C3_4 = -0.4570458;
    const float SH_C3_5 = 1.4453057;
    const float SH_C3_6 = -0.5900436;

    vec3 ShadeSH(vec3 color, 
                vec3 sh1, 
                vec3 sh2, 
                vec3 sh3, 
                vec3 sh4, 
                vec3 sh5, 
                vec3 sh6, 
                vec3 sh7, 
                vec3 sh8, 
                vec3 sh9, 
                vec3 sh10, 
                vec3 sh11, 
                vec3 sh12, 
                vec3 sh13, 
                vec3 sh14, 
                vec3 sh15,
                vec3 dir, 
                int shOrder, 
                bool onlySH)
    {
        //dir = -dir;

        float x = dir.x;
        float y = dir.y;
        float z = dir.z;
        
        // ambient band
        vec3 res = color; // col = sh0 * SH_C0 + 0.5 is already precomputed
        if (onlySH)
            res = vec3(0.5, 0.5, 0.5);
        // 1st degree
        if (shOrder >= 1)
        {
            res += SH_C1 * (-sh1 * y + sh2 * z - sh3 * x);
            // 2nd degree
            if (shOrder >= 2)
            {
                float xx = x * x;
                float yy = y * y;
                float zz = z * z;
                float xy = x * y;
                float yz = y * z;
                float xz = x * z;
                res +=
                    (SH_C2_0 * xy) * sh4 +
                    (SH_C2_1 * yz) * sh5 +
                    (SH_C2_2 * (2 * zz - xx - yy)) * sh6 +
                    (SH_C2_3 * xz) * sh7 +
                    (SH_C2_4 * (xx - yy)) * sh8;
                // 3rd degree
                if (shOrder >= 3)
                {
                    res +=
                        (SH_C3_0 * y * (3 * xx - yy)) * sh9 +
                        (SH_C3_1 * xy * z) * sh10 +
                        (SH_C3_2 * y * (4 * zz - xx - yy)) * sh11 +
                        (SH_C3_3 * z * (2 * zz - 3 * xx - 3 * yy)) * sh12 +
                        (SH_C3_4 * x * (4 * zz - xx - yy)) * sh13 +
                        (SH_C3_5 * z * (xx - yy)) * sh14 +
                        (SH_C3_6 * x * (xx - 3 * yy)) * sh15;
                }
            }
        }
        return max(res, vec3(0,0,0));
    }

)glsl";

#endif // __GSPLAT_SHADER_CORE_LIB__
