#include "GSplatRenderer.h" 
#include "GR_GSplat.h"

#include <UT/UT_Set.h>
#include <UT/UT_UniquePtr.h>
#include <RE/RE_ShaderHandle.h>
#include <RE/RE_OGLBuffer.h>
#include <execution> 
#include <numeric>
#include <algorithm>


GSplatRenderer::GSplatRenderer()
{
    myTriangleGeo = new RE_Geometry;
    const int verticesPerQuad = 6;
    myTriangleGeo->setNumPoints(verticesPerQuad);
    
    initialiseTextureResources();
    
    myPreviousCameraPos = UT_Vector3F(0, 0, 0); 
    mySortDistanceAccum = 0.0;
    myIsFreshGeometry = true;
    myIsShDataPresent = false;
    myIsRenderEnabled = true;
    myCanRender = false;

    mySplatCount = 0;
    myAllocatedSplatCount = 0;
}

void GSplatRenderer::freeTextureResources()
{
    myTexSortedIndexNormalised->free();
    myTexGsplatColorAlphaScaleOrient->free();
    myTexGsplatSh->free();

    myTexSortedIndexNormalised = NULL;
    myTexGsplatColorAlphaScaleOrient = NULL;
    myTexGsplatSh = NULL;

    myGSplatSortedIndexTexDim = 0;
    myGSplatColorAlphaScaleOrientTexDim = 0;
    myGSplatShTexDim = 0;
}

void GSplatRenderer::initialiseTextureResources()
{
    myTexSortedIndexNormalised = RE_Texture::newTexture(RE_TEXTURE_2D);
    myTexSortedIndexNormalised->setFormat(RE_GPU_FLOAT32, 1);
    myGSplatSortedIndexTexDim = 0;
    
    myTexGsplatColorAlphaScaleOrient = RE_Texture::newTexture(RE_TEXTURE_2D);
    myTexGsplatColorAlphaScaleOrient->setFormat(RE_GPU_FLOAT16, 4); //RGBA
    myGSplatColorAlphaScaleOrientTexDim = 0;

    myTexGsplatSh = RE_Texture::newTexture(RE_TEXTURE_2D);
    myTexGsplatSh->setFormat(RE_GPU_FLOAT16, 3);
    myGSplatShTexDim = 0;
}

void GSplatRenderer::allocateTextureResources(RE_RenderContext r)
{
    int newGSplatSortedIndexTexDim = closestSqrtPowerOf2(mySplatCount);
    int newGSplatColorAlphaScaleOrientTexDim = closestSqrtPowerOf2(mySplatCount * 4); //RGBA, ORIENT, SCALE // TODO: all PCull?
    int newGSplatShTexDim = -1;
    
    if (myIsShDataPresent)
    {
        newGSplatShTexDim = closestSqrtPowerOf2(mySplatCount * 16); //16 to keep power of two.
    }

    if (newGSplatSortedIndexTexDim != myGSplatSortedIndexTexDim
        || newGSplatColorAlphaScaleOrientTexDim != myGSplatColorAlphaScaleOrientTexDim
        || newGSplatShTexDim != myGSplatShTexDim)
    {
        //freeTextureResources();
        //initialiseTextureResources();

        myGSplatSortedIndexTexDim = newGSplatSortedIndexTexDim;
        myTexSortedIndexNormalised->setResolution(myGSplatSortedIndexTexDim, myGSplatSortedIndexTexDim);
        //myTexSortedIndexNormalised->setTexture(r, nullptr);
        
        myGSplatColorAlphaScaleOrientTexDim = newGSplatColorAlphaScaleOrientTexDim;
        myTexGsplatColorAlphaScaleOrient->setResolution(myGSplatColorAlphaScaleOrientTexDim, myGSplatColorAlphaScaleOrientTexDim);
        //myTexGsplatColorAlphaScaleOrient->setTexture(r, nullptr);
        
        myGSplatShTexDim = 0;
        if (myIsShDataPresent)
        {
            myGSplatShTexDim = newGSplatShTexDim;
            myTexGsplatSh->setResolution(myGSplatShTexDim, myGSplatShTexDim);
            //myTexGsplatSh->setTexture(r, nullptr);
        }
    }
}

bool GSplatRenderer::isRenderStateRegistryCurrent() 
{
    UT_Set<std::string> registryIdsWithRequestedRender;
    for (UT_Map<std::string, std::unique_ptr<GSplatRegisterEntry>>::const_iterator it = myRenderStateRegistry.begin(); it != myRenderStateRegistry.end(); ++it) 
    {
        if (it->second->active) 
        {
            registryIdsWithRequestedRender.insert(it->first);
        }
    }

    return myActiveRegistries == registryIdsWithRequestedRender;
}

unsigned int GSplatRenderer::closestSqrtPowerOf2(const int n) 
{
    if (n <= 1) return 2; // The smallest power of 2 whose square is greater than 0 or 1 is 2
    
    float sqrtVal = std::sqrt(n);
    unsigned int power = std::ceil(std::log2(sqrtVal)); // Use ceil to ensure we get the smallest power of 2 greater than or equal to sqrtVal
    
    return std::pow(2, power);
}

bool GSplatRenderer::checkSignificantDelta(const UT_Vector3F& newPos, const UT_Vector3F& oldPos, const float threshold) 
{
    float delta = (newPos - oldPos).length2();
    mySortDistanceAccum += delta;
    if (mySortDistanceAccum > threshold * threshold) 
    {
        return true;
    }
    return false;
}

bool GSplatRenderer::argsortByDistance2(const UT_Vector3F *posSplatPointsData, const UT_Vector3F &cameraPos, const int pointCount) 
{
    bool force = false;
    if (myIsFreshGeometry || myGsplatZDistances.size() != static_cast<size_t>(pointCount)) 
    {
        myIsFreshGeometry = false;
        force = true;
    }
    
    bool sorted = false;
    if (force || checkSignificantDelta(cameraPos, myPreviousCameraPos) || myGsplatZIndices.empty()) 
    {    
        myGsplatZDistances.resize(pointCount);
        myGsplatZIndices.resize(pointCount);
        myGsplatZIndices_f.resize(pointCount);

        // Fill indices with 0, 1, 2, ...
        std::iota(myGsplatZIndices.begin(), myGsplatZIndices.end(), 0); 
        
        tbb::parallel_for(tbb::blocked_range<size_t>(0, pointCount),
            [&](const tbb::blocked_range<size_t>& r) {
                for (size_t i = r.begin(); i != r.end(); ++i) {
                    const UT_Vector3F& pi = posSplatPointsData[i];
                    float dx = pi.x() - cameraPos.x();
                    float dy = pi.y() - cameraPos.y();
                    float dz = pi.z() - cameraPos.z();
                    myGsplatZDistances[i] = dx * dx + dy * dy + dz * dz;
                }
            }
        );

        tbb::parallel_sort(myGsplatZIndices.begin(), myGsplatZIndices.end(),
            [&](size_t i, size_t j) { return myGsplatZDistances[i] < myGsplatZDistances[j]; }
        );

        // Transform indices to normalized values
        tbb::parallel_for(tbb::blocked_range<size_t>(0, pointCount),
            [&](const tbb::blocked_range<size_t>& r) {
                for (size_t i = r.begin(); i != r.end(); ++i) {
                    myGsplatZIndices_f[i] = static_cast<float>(myGsplatZIndices[i]) / pointCount;
                }
            }
        );

        mySortDistanceAccum = 0.0;
        sorted = true;
    }

    myPreviousCameraPos = cameraPos;

    return sorted;
}

std::string GSplatRenderer::registerUpdate(
    
    const GU_Detail *gdp,
    const RE_CacheVersion &gversion, 
    const GA_Offset &gvtx,
    
    const GA_Size &splatCount, 
    const UT_Vector3HArray& splatPts, 
    const UT_Vector3HArray& splatColors,
    const UT_FloatArray& splatAlphas,
    const UT_Vector3HArray& splatScales,
    const UT_Vector4HArray& splatOrients,
    const MyUT_Matrix4HArray& splatShxs,
    const MyUT_Matrix4HArray& splatShys,
    const MyUT_Matrix4HArray& splatShzs) 
{

    // if there are entries in the registry for this gdp, 
    // gversion will be different from the cache version of the entry
    // that'll be a good moment to flush those entries
    // the other use case where we flush is when the gdp is deleted
    // but that does not happen on this register function.
    std::ostringstream oss;
    oss << std::hex << std::showbase << reinterpret_cast<uintptr_t>(gdp) << "__" << std::dec << gvtx  << "__" << gversion.getElement(0) << "_" << gversion.getElement(1) << "_" << gversion.getElement(2) << "_" << gversion.getElement(3);
    std::string registryId = oss.str();
    
    // go over the entries in the registry and if there is a gdp match and version is different, remove the entry
    for (UT_Map<std::string, std::unique_ptr<GSplatRegisterEntry>>::iterator it = myRenderStateRegistry.begin(); it != myRenderStateRegistry.end(); ) 
    {
        std::string primHandle = it->first;
        std::unique_ptr<GSplatRegisterEntry>& gsplatEntryPtr = it->second;

        if (gsplatEntryPtr->gdp == gdp)
        {
            if (gsplatEntryPtr->gversion != gversion) 
            {
                it = myRenderStateRegistry.erase(it);
            } 
            else 
            {
                ++it;
            }
        } 
        else 
        {
            ++it;
        }
    }
    
    UT_Map<std::string, std::unique_ptr<GSplatRegisterEntry>>::iterator it = myRenderStateRegistry.find(registryId);
    if (it == myRenderStateRegistry.end()) 
    {
        myRenderStateRegistry[registryId] = std::make_unique<GSplatRegisterEntry>();
    }

    myRenderStateRegistry[registryId]->gversion = gversion;
    myRenderStateRegistry[registryId]->gdp = const_cast<GU_Detail*>(gdp);
    myRenderStateRegistry[registryId]->gvtx = gvtx;
    myRenderStateRegistry[registryId]->splatPts = const_cast<UT_Vector3HArray*>(&splatPts);
    myRenderStateRegistry[registryId]->splatColors = const_cast<UT_Vector3HArray*>(&splatColors);
    myRenderStateRegistry[registryId]->splatAlphas = const_cast<UT_FloatArray*>(&splatAlphas);
    myRenderStateRegistry[registryId]->splatScales = const_cast<UT_Vector3HArray*>(&splatScales);
    myRenderStateRegistry[registryId]->splatOrients = const_cast<UT_Vector4HArray*>(&splatOrients);
    myRenderStateRegistry[registryId]->splatShxs = const_cast<MyUT_Matrix4HArray*>(&splatShxs);
    myRenderStateRegistry[registryId]->splatShys = const_cast<MyUT_Matrix4HArray*>(&splatShys);
    myRenderStateRegistry[registryId]->splatShzs = const_cast<MyUT_Matrix4HArray*>(&splatShzs);
    myRenderStateRegistry[registryId]->splatCount = splatCount;
    myRenderStateRegistry[registryId]->active = false;
    myRenderStateRegistry[registryId]->age = -1;
    myRenderStateRegistry[registryId]->ageSinceLastActive = -1;

    return registryId;
}

void GSplatRenderer::flushEntriesForMatchingDetail(std::string registryId)
{
    UT_Map<std::string, std::unique_ptr<GSplatRegisterEntry>>::iterator it = myRenderStateRegistry.find(registryId);
    if (it != myRenderStateRegistry.end()) 
    {
        GU_Detail *gdp = it->second->gdp;
        for (UT_Map<std::string, std::unique_ptr<GSplatRegisterEntry>>::iterator it = myRenderStateRegistry.begin(); it != myRenderStateRegistry.end(); ) 
        {
            if (it->second->gdp == gdp) 
            {
                it = myRenderStateRegistry.erase(it);
            } 
            else 
            {
                ++it;
            }
        }
    }
}

void GSplatRenderer::includeInRenderPass(std::string registryId) 
{
    UT_Map<std::string, std::unique_ptr<GSplatRegisterEntry>>::iterator it = myRenderStateRegistry.find(registryId);
    if (it != myRenderStateRegistry.end()) 
    {
        it->second->active = true;
    }
}

void GSplatRenderer::generateRenderGeometry(RE_RenderContext r)
{
    if (isRenderStateRegistryCurrent())
    {
        return;
    }
    else
    {
        myIsFreshGeometry = true;
        myGsplatZDistances.clear();
        myGsplatZIndices.clear();
        myGsplatZIndices_f.clear();
        mySortDistanceAccum = 0.0;
    }

    myActiveRegistries.clear();
    GA_Size totalSplatCount = 0; 
    bool isShDataPresent = true;
    myCanRender = false;
    for (UT_Map<std::string, std::unique_ptr<GSplatRegisterEntry>>::const_iterator it = myRenderStateRegistry.begin(); it != myRenderStateRegistry.end(); ++it)
    {
        if (it->second->active && it->second->splatCount > 0) //TODO: is this count necessary? if so, earlier?
        {
            myActiveRegistries.insert(it->first);
            totalSplatCount += it->second->splatCount;
            isShDataPresent = it->second->splatShxs->size() > 0;
        }
    }

    if (!totalSplatCount)
    {
        return;
    }

    myCanRender = true;
    mySplatCount = totalSplatCount;
    myIsShDataPresent = isShDataPresent;

    const char *posname = "P";

    mySplatPoints.resize(mySplatCount);

    RE_VertexArray *posSplatTriangles = myTriangleGeo->findCachedAttrib(r, posname, RE_GPU_FLOAT16, 3, RE_ARRAY_POINT, true);
    UT_Vector3F *pTriangleGeoData = static_cast<UT_Vector3F *>(posSplatTriangles->map(r));

    if (!pTriangleGeoData)
    {
        return;
    }
        
    allocateTextureResources(r);
    
    std::vector<fpreal16> colorAlphaScaleOrient_data;
    colorAlphaScaleOrient_data.resize(myGSplatColorAlphaScaleOrientTexDim * myGSplatColorAlphaScaleOrientTexDim * 4); // *3 for rgb

    std::vector<fpreal16> sh_data;
    sh_data.resize(myIsShDataPresent ? myGSplatShTexDim * myGSplatShTexDim * 3 : 0);

    int offset = 0;
    for (UT_Set<std::string>::const_iterator it0 = myActiveRegistries.begin(); it0 != myActiveRegistries.end(); ++it0)
    {
        UT_Map<std::string, std::unique_ptr<GSplatRegisterEntry>>::iterator it = myRenderStateRegistry.find(*it0);

        std::string primHandle = it->first;
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

                    if (myIsShDataPresent) 
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
    if (myIsShDataPresent)
    {
        myTexGsplatSh->setTexture(r, sh_data.data());
    }

    myAllocatedSplatCount = totalSplatCount;
}

void GSplatRenderer::render(RE_RenderContext r) 
{
    if (!myIsRenderEnabled || !myCanRender || !myTriangleGeo)
    {
        return;
    }

    bool anythingRenderable = false;
    for (std::pair<const std::string, std::unique_ptr<GSplatRenderer::GSplatRegisterEntry>>& entry : myRenderStateRegistry) {
        anythingRenderable |= entry.second->active;
    }

    if (!anythingRenderable)
    {
        return;
    }

    UT_Matrix4D view_mat;
    r->getMatrix(view_mat);
    view_mat.invert();
    UT_Vector3 camera_pos = UT_Vector3(0,0,0);
    camera_pos = rowVecMult(camera_pos, view_mat);

    int splatCount = mySplatPoints.size();
    if (argsortByDistance2(mySplatPoints.data(), camera_pos, splatCount))
    {
        myGsplatZIndices_f.resize(myGSplatSortedIndexTexDim*myGSplatSortedIndexTexDim);
        myTexSortedIndexNormalised->setTexture(r, myGsplatZIndices_f.data());
    }
    
    // gaussians are rendered after all opaque objects (DM_GSplatHook calls this function after rendering all opaque objects)
    // therefore gaussians must be tested against Z buffer but do not write into it (1)
    // they are also rendered before all transparencies, therefore no interaction with transparencies is supported.    
    RE_Shader* theGSShader = GsplatShaderManager::getInstance().getShader(GsplatShaderManager::GSPLAT_MAIN_SHADER, r);
    r->pushShader(theGSShader);

    // Keep depth buffer check enabled but don't write to it (1)
    r->pushDepthState();
    //r->enableDepthTest();
    r->disableDepthBufferWriting();

    // Enable blending
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

    theGSShader->bindInt(r, "gSplatShEnabled", myIsShDataPresent ? 1 : 0);
    if (myIsShDataPresent)
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
    for (std::pair<const std::string, std::unique_ptr<GSplatRenderer::GSplatRegisterEntry>>& entry : myRenderStateRegistry) {
        if (entry.second->active)
        {
            entry.second->ageSinceLastActive = 0;
        }
        else 
        if (entry.second->ageSinceLastActive > -1)
        {
            ++entry.second->ageSinceLastActive;
        }
        
        entry.second->active = false;
        ++entry.second->age;
    }
}

void GSplatRenderer::setRenderingEnabled(bool isRenderEnabled) 
{
    myIsRenderEnabled = isRenderEnabled;
}
