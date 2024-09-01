/***************************************************************************************/
/*  Filename: GSsplatRenderer.C                                                        */
/*  Description: Main renderer class of the GSplat Plugin                              */
/*                                                                                     */
/*  Copyright (C) 2024 Ruben Diaz                                                      */
/*                                                                                     */
/*  License: AGPL-3.0-or-later                                                         */
/*           https://github.com/rubendhz/houdini-gsplat-renderer/blob/develop/LICENSE  */
/***************************************************************************************/


#include "GSplatPluginVersion.h"
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

    myGSplatCount = 0;
    //myAllocatedSplatCount = 0;

    mySplatOrigin = UT_Vector3(0, 0, 0);
}

void GSplatRenderer::freeTextureResources()
{
    myTexSortedIndex->free();
    myTexGsplatPosColorAlphaScaleOrient->free();
    myTexGsplatShDeg1And2->free();
    myTexGsplatShDeg3->free();

    myTexSortedIndex= NULL;
    myTexGsplatPosColorAlphaScaleOrient = NULL;
    myTexGsplatShDeg1And2 = NULL;
    myTexGsplatShDeg3 = NULL;

    myGSplatSortedIndexTexDim = 0;
    myGSplatPosColorAlphaScaleOrientTexDim = 0;
    myGSplatShDeg1And2TexDim = 0;
    myGSplatShDeg3TexDim = 0;
}

void GSplatRenderer::initialiseTextureResourceCommon(RE_Texture* tex)
{
    tex->setCompression(RE_TextureCompress::RE_COMPRESS_NONE, false);
    tex->setSamples(1);
    tex->setMipMap(false, false);
}

void GSplatRenderer::setTextureFilteringCommon(RE_RenderContext r, RE_Texture* tex)
{
    tex->setMinFilter(r, RE_FILT_NEAREST);
    tex->setMagFilter(r, RE_FILT_NEAREST);
    tex->setTextureWrap(r,RE_CLAMP_EDGE,RE_CLAMP_EDGE);
    tex->setDepthCompareMode(r, false);
}

void GSplatRenderer::initialiseTextureResources()
{
    myTexSortedIndex = RE_Texture::newTexture(RE_TEXTURE_2D);
    myTexSortedIndex->setDataType(RE_TEXTURE_DATA_INTEGER);
    myTexSortedIndex->setFormat(RE_GPU_INT32, 1);
    myTexSortedIndex->setClientFormat(RE_GPU_INT32, 1);
    initialiseTextureResourceCommon(myTexSortedIndex);
    myGSplatSortedIndexTexDim = 0;
    
    myTexGsplatPosColorAlphaScaleOrient = RE_Texture::newTexture(RE_TEXTURE_2D);
    myTexGsplatPosColorAlphaScaleOrient->setFormat(RE_GPU_FLOAT16, 4); //RGBA
    initialiseTextureResourceCommon(myTexGsplatPosColorAlphaScaleOrient);
    myGSplatPosColorAlphaScaleOrientTexDim = 0;

    myTexGsplatShDeg1And2 = RE_Texture::newTexture(RE_TEXTURE_2D);
    myTexGsplatShDeg1And2->setFormat(RE_GPU_FLOAT16, 3);
    initialiseTextureResourceCommon(myTexGsplatShDeg1And2);
    myGSplatShDeg1And2TexDim = 0;

    myTexGsplatShDeg3 = RE_Texture::newTexture(RE_TEXTURE_2D);
    myTexGsplatShDeg3->setFormat(RE_GPU_FLOAT16, 3);
    initialiseTextureResourceCommon(myTexGsplatShDeg3);
    myGSplatShDeg3TexDim = 0;
}

void GSplatRenderer::allocateTextureResources(RE_RenderContext r)
{
    int newGSplatSortedIndexTexDim = closestSqrtPowerOf2(myGSplatCount);
    int newGSplatColorAlphaScaleOrientTexDim = closestSqrtPowerOf2(myGSplatCount * 4); //RGBA, ORIENT, SCALE // TODO: all PCull?
    int newGSplatShTexDim = -1;
    
    if (myIsShDataPresent)
    {
        // We are going to store SH in two textures, one for deg 1 (1 to 8) and 2 (9 to 15), and another for deg 3
        // we pad deg 3 texture to 8 to keep power of two.
        newGSplatShTexDim = closestSqrtPowerOf2(myGSplatCount * 8); 
    }

    if (newGSplatSortedIndexTexDim != myGSplatSortedIndexTexDim
        || newGSplatColorAlphaScaleOrientTexDim != myGSplatPosColorAlphaScaleOrientTexDim
        || newGSplatShTexDim != myGSplatShDeg1And2TexDim)
    {
        myGSplatSortedIndexTexDim = newGSplatSortedIndexTexDim;
        myTexSortedIndex->setResolution(myGSplatSortedIndexTexDim, myGSplatSortedIndexTexDim);

        myGSplatPosColorAlphaScaleOrientTexDim = newGSplatColorAlphaScaleOrientTexDim;
        myTexGsplatPosColorAlphaScaleOrient->setResolution(myGSplatPosColorAlphaScaleOrientTexDim, myGSplatPosColorAlphaScaleOrientTexDim);
        
        myGSplatShDeg1And2TexDim = 0;
        myGSplatShDeg3TexDim = 0;
        if (myIsShDataPresent)
        {
            myGSplatShDeg1And2TexDim = newGSplatShTexDim;
            myGSplatShDeg3TexDim = newGSplatShTexDim;
            myTexGsplatShDeg1And2->setResolution(myGSplatShDeg1And2TexDim, myGSplatShDeg1And2TexDim);
            myTexGsplatShDeg3->setResolution(myGSplatShDeg3TexDim, myGSplatShDeg3TexDim);
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

bool GSplatRenderer::argsortByDistance(const UT_Vector3F *posSplatPointsData, const UT_Vector3F &cameraPos, const int pointCount) 
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
    const UT_Vector3 &splatOrigin,
    const UT_Vector3Array& splatPts, 
    const UT_Vector3HArray& splatColors,
    const UT_FloatArray& splatAlphas,
    const UT_Vector3HArray& splatScales,
    const UT_Vector4HArray& splatOrients,
    const MyUT_Matrix4HArray& splatShxs,
    const MyUT_Matrix4HArray& splatShys,
    const MyUT_Matrix4HArray& splatShzs) 
{
    printPluginVersionOnce();

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
    myRenderStateRegistry[registryId]->splatOrigin = splatOrigin;
    myRenderStateRegistry[registryId]->splatPts = const_cast<UT_Vector3Array*>(&splatPts);
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
        mySortDistanceAccum = 0.0;
    }

    GA_Size GSplatCountMax = GSPLAT_COUNT_MAX - 1;

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
        if (totalSplatCount >= GSplatCountMax)
        {
            break;
        }
    }

    if (!totalSplatCount)
    {
        return;
    }

    myCanRender = true;
    myGSplatCount = std::min(totalSplatCount, GSplatCountMax);
    myIsShDataPresent = isShDataPresent;

    const char *posname = "P";

    mySplatPoints.resize(myGSplatCount);

    RE_VertexArray *posSplatTriangles = myTriangleGeo->findCachedAttrib(r, posname, RE_GPU_FLOAT16, 3, RE_ARRAY_POINT, true);
    UT_Vector3F *pTriangleGeoData = static_cast<UT_Vector3F *>(posSplatTriangles->map(r));

    if (!pTriangleGeoData)
    {
        return;
    }
        
    allocateTextureResources(r);
    
    std::vector<fpreal16> PosColorAlphaScaleOrient_data;
    PosColorAlphaScaleOrient_data.resize(myGSplatPosColorAlphaScaleOrientTexDim * myGSplatPosColorAlphaScaleOrientTexDim * 4); // *3 for rgb

    std::vector<fpreal16> shDeg1and2_data;
    std::vector<fpreal16> shDeg3_data;
    shDeg1and2_data.resize(myIsShDataPresent ? myGSplatShDeg1And2TexDim * myGSplatShDeg1And2TexDim * 3 : 0);
    shDeg3_data.resize(myIsShDataPresent ? myGSplatShDeg3TexDim * myGSplatShDeg3TexDim * 3 : 0);

    mySplatOrigin = UT_Vector3(0, 0, 0);
    int splatClusters = 0;
    for (UT_Set<std::string>::const_iterator it0 = myActiveRegistries.begin(); it0 != myActiveRegistries.end(); ++it0)
    {
        UT_Map<std::string, std::unique_ptr<GSplatRegisterEntry>>::iterator it = myRenderStateRegistry.find(*it0);
        const GSplatRegisterEntry* entry = myRenderStateRegistry[it->first].get();
        if (entry)
        {
            mySplatOrigin += entry->splatOrigin;
            ++splatClusters;
        }
    }
    if (splatClusters > 0)
    {
        mySplatOrigin /= splatClusters;
    }

    GA_Offset offset = 0;
    for (UT_Set<std::string>::const_iterator it0 = myActiveRegistries.begin(); it0 != myActiveRegistries.end(); ++it0)
    {
        UT_Map<std::string, std::unique_ptr<GSplatRegisterEntry>>::iterator it = myRenderStateRegistry.find(*it0);

        const GSplatRegisterEntry* entry = myRenderStateRegistry[it->first].get();
        if (entry)
        {
            const UT_Vector3Array& splatPts = *entry->splatPts;
            const UT_Vector3HArray& splatColors = *entry->splatColors;
            const UT_FloatArray& splatAlphas = *entry->splatAlphas;
            const UT_Vector3HArray& splatScales = *entry->splatScales;
            const UT_Vector4HArray& splatOrients = *entry->splatOrients;
            const MyUT_Matrix4HArray& splatShxs = *entry->splatShxs;
            const MyUT_Matrix4HArray& splatShys = *entry->splatShys;
            const MyUT_Matrix4HArray& splatShzs = *entry->splatShzs;

            GA_Size splatCount = entry->splatCount;
            GA_Size gsplatBudgetLeft = (GSplatCountMax - offset);
            if (gsplatBudgetLeft <= 0)
            {
                break;
            }
            else
            {
                splatCount = std::min(splatCount, gsplatBudgetLeft);
            }

            tbb::parallel_for(tbb::blocked_range<GA_Size>(0, splatCount), [&](const tbb::blocked_range<GA_Size>& r) 
            {
                for (GA_Offset i = r.begin(); i != r.end(); ++i) 
                {
                    GA_Offset offset_inner = offset + i;

                    mySplatPoints[offset_inner] = splatPts(i);

                    GA_Offset offset_posColorAlphaScaleOrient = offset_inner * 4 * 4; // Calculate the starting index for this point's data

                    // Position and RGBA data processing
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient]     = splatPts(i).x() - mySplatOrigin.x();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 1] = splatPts(i).y() - mySplatOrigin.y();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 2] = splatPts(i).z() - mySplatOrigin.z();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 3] = 0; // Padded zero
                    
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 4]   = splatColors(i).x();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 5]   = splatColors(i).y();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 6]   = splatColors(i).z();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 7]   = splatAlphas(i);
                    //SCALExyz (waste one entry here)
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 8]   = splatScales(i).x();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 9]   = splatScales(i).y();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 10]  = splatScales(i).z();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 11]  = 0; // Padded zero
                    //Orient
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 12]  = splatOrients(i).x();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 13]  = splatOrients(i).y();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 14]  = splatOrients(i).z();
                    PosColorAlphaScaleOrient_data[offset_posColorAlphaScaleOrient + 15]  = splatOrients(i).w();

                    if (myIsShDataPresent) 
                    {
                        GA_Offset offset_sh = offset_inner * 8 * 3; // Adjusted for spherical harmonics data
                        for (int j = 0; j < 15; ++j) 
                        {
                            int row = int(float(j) / 4);
                            int col = j % 4;
                            if (j < 8)
                            {
                                shDeg1and2_data[offset_sh + 3*j]      = splatShxs(i)(row, col);
                                shDeg1and2_data[offset_sh + 3*j + 1]  = splatShys(i)(row, col);
                                shDeg1and2_data[offset_sh + 3*j + 2]  = splatShzs(i)(row, col);
                            }
                            else
                            {
                                shDeg3_data[offset_sh + 3*(j-8)]      = splatShxs(i)(row, col);
                                shDeg3_data[offset_sh + 3*(j-8) + 1]  = splatShys(i)(row, col);
                                shDeg3_data[offset_sh + 3*(j-8) + 2]  = splatShzs(i)(row, col);
                            }
                        }
                        // Padded zeros
                        shDeg3_data[offset_sh + 3*8]      = 0.0;
                        shDeg3_data[offset_sh + 3*8 + 1]  = 0.0;
                        shDeg3_data[offset_sh + 3*8 + 2]  = 0.0;
                    }
                }
            });

            offset += splatCount;
            if (offset >= GSplatCountMax)
            {
                break;
            }
        }
    }

    posSplatTriangles->unmap(r);

    myTriangleGeo->connectAllPrims(r, RE_GEO_SHADED_IDX, RE_PRIM_TRIANGLES, NULL, true);
    
    setTextureFilteringCommon(r, myTexGsplatPosColorAlphaScaleOrient);
    myTexGsplatPosColorAlphaScaleOrient->setTexture(r, PosColorAlphaScaleOrient_data.data());

    if (myIsShDataPresent)
    {
        shDeg1and2_data.resize(myGSplatShDeg1And2TexDim * myGSplatShDeg1And2TexDim);
        setTextureFilteringCommon(r, myTexGsplatShDeg1And2);
        myTexGsplatShDeg1And2->setTexture(r, shDeg1and2_data.data());

        shDeg3_data.resize(myGSplatShDeg3TexDim * myGSplatShDeg3TexDim);
        setTextureFilteringCommon(r, myTexGsplatShDeg3);
        myTexGsplatShDeg3->setTexture(r, shDeg3_data.data());
    }
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
    if (argsortByDistance(mySplatPoints.data(), camera_pos, splatCount))
    {
        int dataEntryCount = myGSplatSortedIndexTexDim*myGSplatSortedIndexTexDim;
        if (myGsplatZIndices.size() != dataEntryCount)
        {
            myGsplatZIndices.resize(dataEntryCount);
            setTextureFilteringCommon(r, myTexSortedIndex);
            myTexSortedIndex->setTexture(r, myGsplatZIndices.data());
        }
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

    theGSShader->bindInt(r, "GSplatCount", splatCount);
    theGSShader->bindInt(r, "GSplatVertexCount", 6);

    //mesuare time
    theGSShader->bindVector(r, "GSplatOrigin", mySplatOrigin);

    theGSShader->bindInt(r, "GSplatZOrderTexDim", myGSplatSortedIndexTexDim);
    
    r->bindTexture(myTexSortedIndex, theGSShader->getUniformTextureUnit("GSplatZOrderIntegerTexSampler"));

    theGSShader->bindInt(r, "GSplatPosColorAlphaScaleOrientTexDim", myGSplatPosColorAlphaScaleOrientTexDim);
    r->bindTexture(myTexGsplatPosColorAlphaScaleOrient, theGSShader->getUniformTextureUnit("GSplatPosColorAlphaScaleOrientTexSampler"));

    theGSShader->bindInt(r, "GSplatShEnabled", myIsShDataPresent ? 1 : 0);
    if (myIsShDataPresent)
    {
        theGSShader->bindVector(r, "WorldSpaceCameraPos", camera_pos);
        theGSShader->bindInt(r, "GSplatShDeg1And2TexDim", myGSplatShDeg1And2TexDim);
        r->bindTexture(myTexGsplatShDeg1And2, theGSShader->getUniformTextureUnit("GSplatShDeg1And2TexSampler"));
        theGSShader->bindInt(r, "GSplatShDeg3TexDim", myGSplatShDeg3TexDim);
        r->bindTexture(myTexGsplatShDeg3, theGSShader->getUniformTextureUnit("GSplatShDeg3TexSampler"));
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
