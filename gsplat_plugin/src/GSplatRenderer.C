#include "GSplatRenderer.h" 
#include "GR_GSplat.h"

#include <UT/UT_Set.h>
#include <UT/UT_UniquePtr.h>
#include <RE/RE_ShaderHandle.h>
#include <RE/RE_OGLBuffer.h>
#include <execution> 
#include <numeric> 


GSplatRenderer::GSplatRenderer() 
{
    myTriangleGeo = NULL;
    myTexSortedIndexNormalised = NULL;
    myGSplatSortedIndexTexDim = 0;
    myTexGsplatSh = NULL;
    myGSplatShTexDim = 0;
    myTexGsplatColorAlphaScaleOrient = NULL;
    myGSplatColorAlphaScaleOrientTexDim = 0;
    previous_ref_pos = UT_Vector3F(0, 0, 0); 
    sort_distance_accum = 0.0;
    firstRun = true;
    myShDataPresent = false;
    render_enabled = true;
    can_render = false;
}

bool GSplatRenderer::isRenderStateRegistryCurrent() 
{
    UT_Set<GT_PrimitiveHandle> registeredPrimitiveHandles;
    for(UT_Map<GT_PrimitiveHandle, std::unique_ptr<GSplatRegisterEntry>>::const_iterator it = myRenderStateRegistry.begin(); it != myRenderStateRegistry.end(); ++it) 
    {
        registeredPrimitiveHandles.insert(it->first);
    }

    UT_Set<GT_PrimitiveHandle> currentPrimitiveHandles;
    for(UT_Map<GT_PrimitiveHandle, RE_CacheVersion>::const_iterator it = myCurrentCacheVersions.begin(); it != myCurrentCacheVersions.end(); ++it) 
    {
        currentPrimitiveHandles.insert(it->first);
    }

    if(registeredPrimitiveHandles != currentPrimitiveHandles) 
    {
        return false;
    }
    
    for(UT_Set<GT_PrimitiveHandle>::const_iterator it = registeredPrimitiveHandles.begin(); it != registeredPrimitiveHandles.end(); ++it) 
    {
        GT_PrimitiveHandle primHandle = *it;
        if(myRenderStateRegistry[primHandle]->cacheVersion != myCurrentCacheVersions[primHandle]) 
        {
            return false;
        }
    }
    return true;
}

unsigned int GSplatRenderer::closestSqrtPowerOf2(const int n) 
{
    if (n <= 1) return 2; // The smallest power of 2 whose square is greater than 0 or 1 is 2
    
    float sqrtVal = std::sqrt(n);
    unsigned int power = std::ceil(std::log2(sqrtVal)); // Use ceil to ensure we get the smallest power of 2 greater than or equal to sqrtVal
    
    return std::pow(2, power);
}

bool GSplatRenderer::isSignificantMovement2(const UT_Vector3F& newPos, const UT_Vector3F& oldPos, const float threshold) 
{
    float delta = (newPos - oldPos).length2();
    sort_distance_accum += delta;
    if (sort_distance_accum > threshold * threshold) 
    {
        return true;
    }
    return false;
}

bool GSplatRenderer::argsortByDistance2(const UT_Vector3F *posSplatPointsData, const UT_Vector3F &ref_pos, const int pointCount) 
{
    // Initialize indices and distances vector if it's the first run or if point count has changed
    bool force = false;
    if (firstRun || zDistances.size() != static_cast<size_t>(pointCount)) 
    {
        firstRun = false;
        force = true;
    }
    
    bool sorted = false;
    // Only recalculate distances and sort if there's been significant movement
    if (force || isSignificantMovement2(ref_pos, previous_ref_pos) || zIndices.empty()) 
    {    
        zDistances.resize(pointCount);
        zIndices.resize(pointCount);
        zIndices_f.resize(pointCount);

        // Fill indices with 0, 1, 2, ...
        std::iota(zIndices.begin(), zIndices.end(), 0); 
        
        // Calculate distances using transform
        std::transform(zIndices.begin(), zIndices.end(), zDistances.begin(),
                    [=, &posSplatPointsData, &ref_pos](size_t i) -> float {
                        const UT_Vector3F& pi = posSplatPointsData[i];
                        return (pi.x() - ref_pos.x()) * (pi.x() - ref_pos.x()) +
                                (pi.y() - ref_pos.y()) * (pi.y() - ref_pos.y()) +
                                (pi.z() - ref_pos.z()) * (pi.z() - ref_pos.z());
                    });

        // Sort indices based on calculated distances
        std::sort(zIndices.begin(), zIndices.end(),
                [&](const size_t i, const size_t j) { return zDistances[i] < zDistances[j]; });

        // Transform indices to normalized values
        //float invPointCount = 1.0f / static_cast<float>(pointCount);
        std::transform(zIndices.begin(), zIndices.end(), zIndices_f.begin(),
                    [pointCount](size_t idx) -> float {
                        return static_cast<float>(idx) / pointCount;
                    });

        sort_distance_accum = 0.0;
        sorted = true;
    }

    previous_ref_pos = ref_pos;

    return sorted;
}


void GSplatRenderer::update(
    const GT_PrimitiveHandle primHandle, 
    const RE_CacheVersion cacheVersion, 
    const GA_Size splatCount, 
    const UT_Vector3HArray& splatPts, 
    const UT_Vector3HArray& splatColors,
    const UT_FloatArray& splatAlphas,
    const UT_Vector3HArray& splatScales,
    const UT_Vector4HArray& splatOrients,
    const MyUT_Matrix4HArray& splatShxs,
    const MyUT_Matrix4HArray& splatShys,
    const MyUT_Matrix4HArray& splatShzs) 
{
    if (!myRenderStateRegistry[primHandle]) 
    {
        myRenderStateRegistry[primHandle] = std::make_unique<GSplatRegisterEntry>();
    } 
    else 
    {
        if (myRenderStateRegistry[primHandle]->cacheVersion != cacheVersion) 
        {
            myRenderStateRegistry[primHandle]->age = -1;
            myRenderStateRegistry[primHandle]->active = false;
        }
    }

    myRenderStateRegistry[primHandle]->cacheVersion = cacheVersion;
    myRenderStateRegistry[primHandle]->splatPts = const_cast<UT_Vector3HArray*>(&splatPts);
    myRenderStateRegistry[primHandle]->splatColors = const_cast<UT_Vector3HArray*>(&splatColors);
    myRenderStateRegistry[primHandle]->splatAlphas = const_cast<UT_FloatArray*>(&splatAlphas);
    myRenderStateRegistry[primHandle]->splatScales = const_cast<UT_Vector3HArray*>(&splatScales);
    myRenderStateRegistry[primHandle]->splatOrients = const_cast<UT_Vector4HArray*>(&splatOrients);
    myRenderStateRegistry[primHandle]->splatShxs = const_cast<MyUT_Matrix4HArray*>(&splatShxs);
    myRenderStateRegistry[primHandle]->splatShys = const_cast<MyUT_Matrix4HArray*>(&splatShys);
    myRenderStateRegistry[primHandle]->splatShzs = const_cast<MyUT_Matrix4HArray*>(&splatShzs);
    myRenderStateRegistry[primHandle]->splatCount = splatCount;

    firstRun = true;
}

void GSplatRenderer::includeInRenderPass(GT_PrimitiveHandle primHandle) 
{
    if (myRenderStateRegistry[primHandle]) 
    {
        ++myRenderStateRegistry[primHandle]->age;
        myRenderStateRegistry[primHandle]->active = true;
    }
}

void GSplatRenderer::generateRenderGeometry(RE_Render *r)
{
    if (isRenderStateRegistryCurrent())
    {
        return;
    }

    myCurrentCacheVersions.clear();
    GA_Size totalSplatCount = 0; 
    bool shDataPresent = true;
    can_render = false;
    for (UT_Map<GT_PrimitiveHandle, std::unique_ptr<GSplatRegisterEntry>>::const_iterator it = myRenderStateRegistry.begin(); it != myRenderStateRegistry.end(); ++it)
    {
        if (it->second->active && it->second->age >= 0 && it->second->splatCount > 0)
        {
            myCurrentCacheVersions[it->first] = it->second->cacheVersion;
            totalSplatCount += it->second->splatCount;

            shDataPresent = it->second->splatShxs->size() > 0;
        }
    }

    if (!totalSplatCount)
    {
        return;
    }

    can_render = true;

    myShDataPresent = shDataPresent;

    const char *posname = "P";
    
    if(!myTriangleGeo)
        myTriangleGeo = new RE_Geometry;

    mySplatPoints.resize(totalSplatCount);

    const int verticesPerQuad = 6;
    myTriangleGeo->setNumPoints(verticesPerQuad); // totalSplatCount * verticesPerQuad if we were not doing GPU instancing

    RE_VertexArray *posSplatTriangles = myTriangleGeo->findCachedAttrib(r, posname, RE_GPU_FLOAT16, 3, RE_ARRAY_POINT, true);
    UT_Vector3F *pTriangleGeoData = static_cast<UT_Vector3F *>(posSplatTriangles->map(r));

    if (pTriangleGeoData)
    {
        myGSplatSortedIndexTexDim = closestSqrtPowerOf2(totalSplatCount);
        myTexSortedIndexNormalised = RE_Texture::newTexture(RE_TEXTURE_2D);
        myTexSortedIndexNormalised->setResolution(myGSplatSortedIndexTexDim, myGSplatSortedIndexTexDim);
        myTexSortedIndexNormalised->setFormat(RE_GPU_FLOAT32, 1);

        myGSplatColorAlphaScaleOrientTexDim = closestSqrtPowerOf2(totalSplatCount * 4); //Pcull?, RGBA, ORIENT, SCALE
        myTexGsplatColorAlphaScaleOrient = RE_Texture::newTexture(RE_TEXTURE_2D);
        myTexGsplatColorAlphaScaleOrient->setResolution(myGSplatColorAlphaScaleOrientTexDim, myGSplatColorAlphaScaleOrientTexDim);
        myTexGsplatColorAlphaScaleOrient->setFormat(RE_GPU_FLOAT16, 4); //RGBA
        int colorAlphaScaleOrientDataEntries = myGSplatColorAlphaScaleOrientTexDim * myGSplatColorAlphaScaleOrientTexDim * 4; // *3 for rgb
        std::vector<fpreal16> colorAlphaScaleOrient_data;
        colorAlphaScaleOrient_data.resize(colorAlphaScaleOrientDataEntries);

        std::vector<fpreal16> sh_data;
        if (shDataPresent)
        {
            myGSplatShTexDim = closestSqrtPowerOf2(totalSplatCount * 16); //16 to keep power of two.
            myTexGsplatSh = RE_Texture::newTexture(RE_TEXTURE_2D);
            myTexGsplatSh->setResolution(myGSplatShTexDim, myGSplatShTexDim);
            myTexGsplatSh->setFormat(RE_GPU_FLOAT16, 3);
            int shDataEntries = myGSplatShTexDim * myGSplatShTexDim * 3;
            sh_data.resize(shDataEntries);
        }
        else
        {
            myGSplatShTexDim = 0;
            sh_data.resize(0);
        }

        // DEBUG print texture dimensions
        // std::cout << "myGSplatSortedIndexTexDim: " << myGSplatSortedIndexTexDim << "x" << myGSplatSortedIndexTexDim << std::endl;
        // std::cout << "myGSplatColorAlphaScaleOrientTexDim: " << myGSplatColorAlphaScaleOrientTexDim << "x" << myGSplatColorAlphaScaleOrientTexDim << std::endl;
        // std::cout << "myGSplatShTexDim: " << myGSplatShTexDim << "x" << myGSplatShTexDim << std::endl;
        // GLint maxTextureSize = 0;
        // glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
        // std::cout << "GL_MAX_TEXTURE_SIZE: " << maxTextureSize << std::endl;

        int offset = 0;
        for (UT_Map<GT_PrimitiveHandle, RE_CacheVersion>::const_iterator it = myCurrentCacheVersions.begin(); it != myCurrentCacheVersions.end(); ++it)
        {
            GT_PrimitiveHandle primHandle = it->first;
            const GSplatRegisterEntry* entry = myRenderStateRegistry[primHandle].get();
            if (entry)
            {
                const UT_Vector3HArray& splatPts = *entry->splatPts;
                const UT_Vector3HArray& splatColors = *entry->splatColors;
                const UT_FloatArray& splatAlphas = *entry->splatAlphas;
                const UT_Vector3HArray& splatScales = *entry->splatScales;
                const UT_Vector4HArray& splatOrients = *entry->splatOrients;
                const MyUT_Matrix4HArray& splatShxs = *entry->splatShxs;
                const MyUT_Matrix4HArray& splatShys = *entry->splatShys;
                const MyUT_Matrix4HArray& splatShzs = *entry->splatShzs;

                GA_Size splatCount = entry->splatCount;

                tbb::parallel_for(tbb::blocked_range<GA_Size>(0, splatCount), [&](const tbb::blocked_range<GA_Size>& r) 
                {
                    for (GA_Size i = r.begin(); i != r.end(); ++i) 
                    {
                        GA_Size ii = i;
                        GA_Size offset_inner = offset + i;

                        mySplatPoints[offset_inner] = splatPts(ii);

                        int offset_colorAlphaScaleOrient = offset_inner * 4 * 4; // Calculate the starting index for this point's data

                        // Position and RGBA data processing
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient]     = splatPts(ii).x();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 1] = splatPts(ii).y();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 2] = splatPts(ii).z();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 3] = 0; // Padded zero
                        
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 4]   = splatColors(ii).x();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 5]   = splatColors(ii).y();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 6]   = splatColors(ii).z();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 7]   = splatAlphas(ii);
                        //SCALExyz (waste one entry here)
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 8]   = splatScales(ii).x();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 9]   = splatScales(ii).y();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 10]  = splatScales(ii).z();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 11]  = 0;
                        //Orient
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 12]  = splatOrients(ii).x();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 13]  = splatOrients(ii).y();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 14]  = splatOrients(ii).z();
                        colorAlphaScaleOrient_data[offset_colorAlphaScaleOrient + 15]  = splatOrients(ii).w();

                        if (shDataPresent) 
                        {
                            int offset_sh = offset_inner * 16 * 3; // Adjusted for spherical harmonics data
                            for (int j = 0; j < 15; ++j) 
                            {
                                int row = int(float(j) / 4);
                                int col = j % 4;
                                sh_data[offset_sh + 3*j]      = splatShxs(ii)(row, col);
                                sh_data[offset_sh + 3*j + 1]  = splatShys(ii)(row, col);
                                sh_data[offset_sh + 3*j + 2]  = splatShzs(ii)(row, col);
                            }
                        }
                    }
                });

                offset += splatCount;
            }
        }
        posSplatTriangles->unmap(r);

        myTriangleGeo->connectAllPrims(r, RE_GEO_SHADED_IDX, RE_PRIM_TRIANGLES, NULL, true);
        
        myTexGsplatColorAlphaScaleOrient->setTexture(r, colorAlphaScaleOrient_data.data());
        if (shDataPresent)
        {
            myTexGsplatSh->setTexture(r, sh_data.data());
        }
    }
}

void GSplatRenderer::render(RE_RenderContext r) 
{
    if (!this->render_enabled || !can_render)
    {
        return;
    }

    UT_Matrix4D view_mat;
    r->getMatrix(view_mat);
    view_mat.invert();
    UT_Vector3 camera_pos = UT_Vector3(0,0,0);
    camera_pos = rowVecMult(camera_pos, view_mat);

    if (!myTriangleGeo) 
    {
        return;
    }

    int splatCount = mySplatPoints.size();

    if (argsortByDistance2(mySplatPoints.data(), camera_pos, splatCount))
    {
        myTexSortedIndexNormalised->setTexture(r, zIndices_f.data());
    }
    

    // gaussians are rendered after all opaque objects (DM_GSplatHook calls this function after rendering all opaque objects)
    // therefore gaussians must be tested against Z buffer but do not write into it (1)
    // gaussians are rendered before all transparencies, therefore no interaction with transparencies is supported.    
    RE_Shader* theGSShader = GsplatShaderManager::getInstance().getShader(GsplatShaderManager::GSPLAT_MAIN_SHADER, r);
    r->pushShader(theGSShader);

    // Keep depth buffer check enabled but don't write to it (1)
    r->pushBlendState();
    r->blend(1); 
    r->setBlendFunction(RE_SBLEND_ONE_MINUS_DST_ALPHA, RE_DBLEND_ONE); 
    r->setAlphaBlendFunction(RE_SBLEND_ONE_MINUS_DST_ALPHA, RE_DBLEND_ONE);

    if (r->getBlendEquation() != RE_BLEND_ADD)
    {
        r->setBlendEquation(RE_BLEND_ADD);
    }

    theGSShader->bindInt(r, "gSplatCount", splatCount);
    theGSShader->bindInt(r, "gSplatVertexCount", 6);
    
    theGSShader->bindInt(r, "gSplatZOrderTexDim", myGSplatSortedIndexTexDim);
    r->bindTexture(myTexSortedIndexNormalised, theGSShader->getUniformTextureUnit("gSplatZOrderTexSampler"));
    
    theGSShader->bindInt(r, "gSplatColorAlphaScaleOrientTexDim", myGSplatColorAlphaScaleOrientTexDim);
    r->bindTexture(myTexGsplatColorAlphaScaleOrient, theGSShader->getUniformTextureUnit("gSplatColorAlphaScaleOrientTexSampler"));

    theGSShader->bindInt(r, "gSplatShEnabled", myShDataPresent ? 1 : 0);
    if (myShDataPresent)
    {
        theGSShader->bindVector(r, "WorldSpaceCameraPos", camera_pos);
        theGSShader->bindInt(r, "gSplatShTexDim", myGSplatShTexDim);
        r->bindTexture(myTexGsplatSh, theGSShader->getUniformTextureUnit("gSplatShTexSampler"));
    }

    myTriangleGeo->drawInstanced(r, RE_GEO_SHADED_IDX, splatCount); // non instanced version: myTriangleGeo->draw(r, RE_GEO_SHADED_IDX);

    if(r->getShader())
        r->getShader()->removeOverrideBlocks();

    r->enableDepthBufferWriting();
    
    r->popBlendState();
    r->popDepthState();
    

    r->popShader();
}

void GSplatRenderer::postRender() 
{
    for (std::pair<const GT_PrimitiveHandle, std::unique_ptr<GSplatRenderer::GSplatRegisterEntry>>& entry : myRenderStateRegistry) {
        entry.second->active = false;
    }
}

void GSplatRenderer::purgeUnused() 
{
    for (UT_Map<GT_PrimitiveHandle, std::unique_ptr<GSplatRenderer::GSplatRegisterEntry>>::iterator it = myRenderStateRegistry.begin(); it != myRenderStateRegistry.end(); ) 
    {
        if (!it->first || !it->second->active) 
        {
            it = myRenderStateRegistry.erase(it);
        } 
        else 
        {
            ++it;
        }
    }
}

void GSplatRenderer::pprint() 
{
    std::cout << "GPLAT REGISTRY:" << std::endl;
    for (std::pair<const GT_PrimitiveHandle, std::unique_ptr<GSplatRenderer::GSplatRegisterEntry>>& entry : myRenderStateRegistry) 
    {
        std::cout << "Gsplat: " << entry.first << " (age: " << entry.second->age << ")" << " (splat_count: " << entry.second->splatCount << ")" << " (version: " << entry.second->cacheVersion << ")" << std::endl;
    }
    std::cout << "--" << std::endl;
}

void GSplatRenderer::setRenderingEnabled(bool render_enabled) 
{
    this->render_enabled = render_enabled;
}
