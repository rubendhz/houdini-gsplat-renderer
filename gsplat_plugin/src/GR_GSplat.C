/***************************************************************************************/
/*  Filename: GR_GSplat.C                                                              */
/*  Description: Custom Rendering class for GSplat primitives                          */
/*                                                                                     */
/*  Copyright (C) 2024 Ruben Diaz                                                      */
/*                                                                                     */
/*  License: AGPL-3.0-or-later                                                         */
/*           https://github.com/rubendhz/houdini-gsplat-renderer/blob/develop/LICENSE  */
/***************************************************************************************/


#include "GR_GSplat.h"

#include "GSplatRenderer.h"
#include "GSplatShaderManager.h"
#include "GSplatLogger.h"

#include <DM/DM_RenderTable.h>
#include <GR/GR_Utils.h>
#include <RE/RE_ElementArray.h>
#include <RE/RE_Geometry.h>
#include <RE/RE_LightList.h>
#include <RE/RE_ShaderHandle.h>
#include <RE/RE_VertexArray.h>
#include <GT/GT_GEOPrimitive.h>
#include <GA/GA_Iterator.h>
#include <OP/OP_Node.h>



GR_PrimGsplatHook::GR_PrimGsplatHook()
    : GUI_PrimitiveHook("GSplat")
{
}


GR_PrimGsplatHook::~GR_PrimGsplatHook()
{
}


GR_Primitive *
GR_PrimGsplatHook::createPrimitive(
	const GT_PrimitiveHandle &gt_prim,
	const GEO_Primitive	     *geo_prim,
	const GR_RenderInfo	     *info,
	const char		         *cache_name,
	GR_PrimAcceptResult      &processed)
{
    return new GR_PrimGsplat(info, cache_name, geo_prim);
}

GR_PrimGsplat::GR_PrimGsplat(
	const GR_RenderInfo *info,
	const char *cache_name,
	const GEO_Primitive *prim)
    : GR_Primitive(info, cache_name, GA_PrimCompat::TypeMask(0))
{
    myID = prim->getTypeId().get();
	myWireframeGeo = NULL;
}

GR_PrimGsplat::~GR_PrimGsplat()
{
	if (myRegistryId != "")
	{
		GSplatRenderer::getInstance().flushEntriesForMatchingDetail(myRegistryId);
	}	
	delete myWireframeGeo;
}

GR_PrimAcceptResult
GR_PrimGsplat::acceptPrimitive(
	GT_PrimitiveType t,
	int geo_type,
	const GT_PrimitiveHandle &ph,
	const GEO_Primitive *prim)
{
    if(geo_type == myID)
		return GR_PROCESSED;
    
    return GR_NOT_PROCESSED;
}

unsigned int closestSqrtPowerOf2(int n) 
{
    if (n <= 1) return 2; // The smallest power of 2 whose square is greater than 0 or 1 is 2
    float sqrtVal = std::sqrt(n);
    unsigned int power = std::ceil(std::log2(sqrtVal)); // ceil to ensure we get the smallest power of 2 >= sqrtVal
    return std::pow(2, power);
}

bool GR_PrimGsplat::initSHHandle(const GU_Detail *gdp, SHHandles& handles, const char* name, int index) {
	const GA_Attribute *attr = gdp->findPointAttribute(name);
	if (!attr) {
		return false;
	}
	handles.sh[index] = GA_ROHandleV3(attr);
	if (!handles.sh[index].isValid()) {
		return false;
	}
	return true;
}

bool GR_PrimGsplat::initSHHandleFallback(const GU_Detail *gdp, SHHandles& handles, const char* name, int index) {
	const GA_Attribute *attr = gdp->findPointAttribute(name);
	if (!attr) {
		return false;
	}
	handles.sh_fallback[index] = GA_ROHandleF(attr);
	if (!handles.sh_fallback[index].isValid()) {
		return false;
	}
	return true;
}

bool GR_PrimGsplat::initAllSHHandles(const GU_Detail *gdp, SHHandles& handles) {
	const char* names[] = {"sh1", "sh2", "sh3", "sh4", "sh5", "sh6", "sh7", "sh8", "sh9", 
							"sh10", "sh11", "sh12", "sh13", "sh14", "sh15"};
	handles.fallback = false;
	handles.valid = true;
	for (int i = 0; i < 15; ++i) {
		if (!initSHHandle(gdp, handles, names[i], i))
		{
			handles.fallback = true;
			break;
		}
	}

	if (handles.fallback)
	{
		GSplatLogger::getInstance().log(GSplatLogger::LogLevel::_WARNING_, "%s", "Spherical harmonics attributes 'sh1, sh2, ..., sh15' not found. Trying fallback f_rest_X attributes...");
		const char* name_template = "f_rest_%d";
		char name_i[50];
		for (int i = 0; i < 45; ++i) {
			sprintf(name_i, name_template, i);
			if (!initSHHandleFallback(gdp, handles, name_i, i))
			{
				handles.valid = false;
				break;
			}
		}

		if (!handles.valid)
		{
			GSplatLogger::getInstance().log(GSplatLogger::LogLevel::_WARNING_, "%s", "Spherical harmonics fallback 'f_rest_X' attributes not found.");
		}
		else
		{
			GSplatLogger::getInstance().log(GSplatLogger::LogLevel::_INFO_, "%s", "Spherical harmonics fallback 'f_rest_X' found.");
		}
	}
	return handles.valid;
}

void
GR_PrimGsplat::update(
	RE_RenderContext          r,
	const GT_PrimitiveHandle  &primh,
	const GR_UpdateParms      &p)
{
	// Fetch the GEO primitive from the GT primitive handle
    const GEO_PrimGsplat *gSplatPrim = NULL;
    
    // GL3 and above requires named vertex attributes, while GL2 and GL1 use
    // the older builtin names.
    const char *posname = "P";
	const char *colorname = "Cd";
	const char *orientname = "orient";
	const char *scalename = "scale";

	if(!myWireframeGeo)
		myWireframeGeo = new RE_Geometry;
    myWireframeGeo->cacheBuffers(getCacheName());

    getGEOPrimFromGT<GEO_PrimGsplat>(primh, gSplatPrim);

	if(!gSplatPrim || gSplatPrim->getVertexCount() == 0)
    {
		delete myWireframeGeo;
		myWireframeGeo = NULL;
		return;
    }
    
	GU_DetailHandleAutoReadLock georl(p.geometry);
	const GU_Detail *dtl = georl.getGdp();

	const GA_Attribute *cdAttr = dtl->findPointAttribute("Cd");
	if (!cdAttr) 
	{
		GSplatLogger::getInstance().log(GSplatLogger::LogLevel::_ERROR_, "%s", "Color attribute 'Cd' not found!");
	}
	GA_ROHandleV3 colorHandle(cdAttr);

	const GA_Attribute *alphaAttr = dtl->findPointAttribute("opacity");
	const GA_Attribute *alphaFallbackAttr = dtl->findPointAttribute("Alpha");
	if (!alphaAttr && !alphaFallbackAttr) 
	{
		GSplatLogger::getInstance().log(GSplatLogger::LogLevel::_ERROR_, "%s", "Opacity attribute not found! (tried 'opacity' and 'Alpha')");
	} 
	// If both are present, use the "fallback". If only one is present, use that.
	// This is to allow for backwards compatibility with GSOPs Import which provides both "opacity" and "Alpha" 
	bool useAlphaFallback = (alphaFallbackAttr != nullptr);
	GA_ROHandleF alphaHandle;
	if (useAlphaFallback)
	{
		alphaHandle = GA_ROHandleF(alphaFallbackAttr);
	}
	else
	{
		alphaHandle = GA_ROHandleF(alphaAttr);
	}

	const GA_Attribute *scaleAttr = dtl->findPointAttribute("scale");
	if (!scaleAttr) 
	{
		GSplatLogger::getInstance().log(GSplatLogger::LogLevel::_ERROR_, "%s", "Scale attribute 'scale' not found!");
	}
	GA_ROHandleV3 scaleHandle(scaleAttr);

	const GA_Attribute *orientAttr = dtl->findPointAttribute("orient");
	if (!orientAttr) 
	{
		GSplatLogger::getInstance().log(GSplatLogger::LogLevel::_ERROR_, "%s", "Orientation attribute 'orient' not found!");
	}
	GA_ROHandleV4 orientHandle(orientAttr);

	SHHandles shHandles;
	bool sh_data_found = initAllSHHandles(dtl, shHandles);

	const GA_Attribute *explicitCameraPosAttr = dtl->findAttribute(GA_ATTRIB_GLOBAL, "gsplat__explicit_camera_pos");
	GA_ROHandleV3 explicitCameraPosHandle;
	if (explicitCameraPosAttr) 
	{
		explicitCameraPosHandle = GA_ROHandleV3(explicitCameraPosAttr);
	}

	const GA_Attribute *shOrderAttr = dtl->findAttribute(GA_ATTRIB_GLOBAL, "gsplat__sh_order");
	GA_ROHandleI shOrderHandle;
	if (shOrderAttr) 
	{
		shOrderHandle = GA_ROHandleI(shOrderAttr);
	}

	myGsplatCount = gSplatPrim->getVertexCount(); // Now this represents the count for the current primitive only
	mySplatPts.setSize(myGsplatCount);
	mySplatColors.setSize(myGsplatCount);
	mySplatAlphas.setSize(myGsplatCount);
	mySplatScales.setSize(myGsplatCount);
	mySplatOrients.setSize(myGsplatCount);
	
	myShxs.setSize(sh_data_found ? myGsplatCount : 0);
	myShys.setSize(sh_data_found ? myGsplatCount : 0);
	myShzs.setSize(sh_data_found ? myGsplatCount : 0);
	
	tbb::parallel_for(tbb::blocked_range<GA_Size>(0, myGsplatCount),
		[&](const tbb::blocked_range<GA_Size>& r) 
		{
			for (GA_Size i = r.begin(); i != r.end(); ++i) 
			{
				const GA_Offset ptoff = gSplatPrim->getVertexOffset(i);
				const UT_Vector3 pos = dtl->getPos3(ptoff);
				const UT_Vector3 color = colorHandle.isValid() ? colorHandle.get(ptoff) : UT_Vector3(0.0, 0.0, 0.0);
				const float alpha = alphaHandle.isValid() ? alphaHandle.get(ptoff) : 1.0;
				const UT_Vector3 scale = scaleHandle.isValid() ? scaleHandle.get(ptoff) : UT_Vector3(1.0, 1.0, 1.0);
				const UT_Vector4 orient = orientHandle.isValid() ? orientHandle.get(ptoff) : UT_Vector4(0.0, 0.0, 0.0, 1.0);

				mySplatPts[i] = pos;
				mySplatColors[i] = UT_Vector3H(color);
				mySplatAlphas[i] = alpha;
				mySplatScales[i] = UT_Vector3H(scale);
				mySplatOrients[i] = UT_Vector4H(orient);

				if (sh_data_found)
				{
					myShxs[i] = UT_Matrix4F(0.0);
					myShys[i] = UT_Matrix4F(0.0);
					myShzs[i] = UT_Matrix4F(0.0);
					
					if (!shHandles.fallback) 
					{
						for (int j = 0; j < 15; ++j) 
						{
							UT_Vector3 shValue = shHandles.sh[j].get(ptoff);
							int row = int(float(j) / 4);  
							int col = j % 4;
							myShxs[i](row, col) = shValue.x();
							myShys[i](row, col) = shValue.y();
							myShzs[i](row, col) = shValue.z();
						}
					}
					else
					{
						for (int j = 0; j < 15; ++j) 
						{
							float shValue0 = shHandles.sh_fallback[j].get(ptoff);
							float shValue1 = shHandles.sh_fallback[j+15].get(ptoff);
							float shValue2 = shHandles.sh_fallback[j+30].get(ptoff);
							int row = int(float(j) / 4);  
							int col = j % 4;
							myShxs[i](row, col) = shValue0;
							myShys[i](row, col) = shValue1;
							myShzs[i](row, col) = shValue2;
						}
					}
				}
			}
		}
	);

	GR_UpdateParms dp(p);

	const int verticesPerQuad_wireframe = 8;
	myWireframeGeo->setNumPoints(myGsplatCount * verticesPerQuad_wireframe);
	RE_VertexArray *posWire = myWireframeGeo->findCachedAttrib(r, posname, RE_GPU_FLOAT32, 3, RE_ARRAY_POINT, true);
	RE_VertexArray *colorWire = myWireframeGeo->findCachedAttrib(r, colorname, RE_GPU_FLOAT16, 3, RE_ARRAY_POINT, true);
    RE_VertexArray *orientWire = myWireframeGeo->findCachedAttrib(r, orientname, RE_GPU_FLOAT16, 4, RE_ARRAY_POINT, true);
	RE_VertexArray *scaleWire = myWireframeGeo->findCachedAttrib(r, scalename, RE_GPU_FLOAT16, 3, RE_ARRAY_POINT, true);

	if(posWire->getCacheVersion() != dp.geo_version)
    {
		UT_Vector3 *pdata = static_cast<UT_Vector3 *>(posWire->map(r));
		UT_Vector3H *colordata = static_cast<UT_Vector3H *>(colorWire->map(r));
		UT_Vector4H *orientdata = static_cast<UT_Vector4H *>(orientWire->map(r));
		UT_Vector3H *scaledata = static_cast<UT_Vector3H *>(scaleWire->map(r));

		if(pdata && colordata && orientdata && scaledata)
		{
			int verticesPerQuad = 8;
			tbb::parallel_for(tbb::blocked_range<int>(0, myGsplatCount),
				[&](const tbb::blocked_range<int>& r) {
					for(int t = r.begin(); t != r.end(); ++t) {
						int offset = t * verticesPerQuad_wireframe;
						for(int vtx = 0; vtx < verticesPerQuad; ++vtx) {
							pdata[offset + vtx] = mySplatPts(t);
							colordata[offset + vtx] = mySplatColors(t);
							orientdata[offset + vtx] = mySplatOrients(t);
							scaledata[offset + vtx] = mySplatScales(t);
						}
					}
				}
			);

			// unmap the buffer so it can be used by GL
			posWire->unmap(r);
			colorWire->unmap(r);
			orientWire->unmap(r);
			scaleWire->unmap(r);
			
			// set the cache version after assigning data.
			posWire->setCacheVersion(dp.geo_version);
			colorWire->setCacheVersion(dp.geo_version);
			orientWire->setCacheVersion(dp.geo_version);
			scaleWire->setCacheVersion(dp.geo_version);
		}
    }

	myWireframeGeo->connectAllPrims(r, RE_GEO_WIRE_IDX, RE_PRIM_LINES, NULL, true);

	myRegistryId = GSplatRenderer::getInstance().registerUpdate(
										 dtl,
										 dp.geo_version, 
										 gSplatPrim->getVertexOffset(0),
										 myGsplatCount,
										 gSplatPrim->baryCenter(),
										 mySplatPts, 
										 mySplatColors, 
										 mySplatAlphas,
										 mySplatScales,
										 mySplatOrients,
										 myShxs,
										 myShys,
										 myShzs);
	
	mySetExplicitCameraPos = explicitCameraPosHandle.isValid();
	if (mySetExplicitCameraPos)
	{
		myExplicitCameraPos = explicitCameraPosHandle.get(0);
	}

	myShOrder = 3;
	if (shOrderHandle.isValid())
	{
		myShOrder = shOrderHandle.get(0);
		if (myShOrder < 0 || myShOrder > 3)
		{
			GSplatLogger::getInstance().log(GSplatLogger::LogLevel::_ERROR_, "Spherical harmonics order requested: %d. Allowed values are 0, 1, 2, 3. Contribution will be disabled.", myShOrder);
			myShOrder = 0;
		}
	}
}

void
GR_PrimGsplat::render(
	RE_RenderContext	r,
	GR_RenderMode	    render_mode,
	GR_RenderFlags	    flags,
	GR_DrawParms	    dp)
{
	if(!myWireframeGeo)
	{
		return;
	}

	GSplatRenderer::getInstance().setRenderingEnabled(render_mode < GR_RENDER_NUM_BEAUTY_MODES); //TODO, pass in r here, as different viewports could have different render modes.

	bool need_wire =(render_mode == GR_RENDER_WIREFRAME) ||
		      		(flags & GR_RENDER_FLAG_WIRE_OVER);

    if(need_wire)
    {
		RE_Shader* sh = GsplatShaderManager::getInstance().getShader(GsplatShaderManager::GSPLAT_WIRE_SHADER, r);
		r->pushShader(sh);
		myWireframeGeo->draw(r, RE_GEO_WIRE_IDX);
		r->popShader();
	}

	GSplatRenderer::getInstance().includeInRenderPass(myRegistryId);

	if (mySetExplicitCameraPos)
	{
		GSplatRenderer::getInstance().setExplicitCameraPos(myExplicitCameraPos);
	}

	GSplatRenderer::getInstance().setSphericalHarmonicsOrder(myShOrder);
}

void
GR_PrimGsplat::renderDecoration(
	RE_RenderContext r,
	GR_Decoration decor,
	const GR_DecorationParms &p)
{
	return;
}

int
GR_PrimGsplat::renderPick(RE_RenderContext r,
			 const GR_DisplayOption *opt,
			 unsigned int pick_type,
			 GR_PickStyle pick_style,
			 bool has_pick_map)
{
    // TODO: make pickable
    return 0;
}
