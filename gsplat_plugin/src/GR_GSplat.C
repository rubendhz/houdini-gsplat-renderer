#include "GR_GSplat.h"

#include "GSplatRenderer.h"
#include "GSplatShaderManager.h"

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

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>



GR_PrimGsplatHook::GR_PrimGsplatHook()
    : GUI_PrimitiveHook("Gsplat")
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
	//myPrimitiveHandle = NULL; // only to store the prim handle between update and render calls
}

GR_PrimGsplat::~GR_PrimGsplat()
{
	//std::cout << "GR_PrimGsplat::destroy - primHandle " << myPrimitiveHandle << std::endl;
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

// std::string GR_PrimGsplat::generatePrimID(const GU_Detail *dtl, const GEO_PrimGsplat *prim)
// {
// 	std::ostringstream oss;
//     oss << std::hex << std::showbase << reinterpret_cast<uintptr_t>(dtl) << "__" << std::dec << prim->getPointIndex(0);
// 	myGsplatStrId = oss.str();
// 	return myGsplatStrId;
// }

// std::string GR_PrimGsplat::getPrimID()
// {
// 	return myGsplatStrId;
// }

void
GR_PrimGsplat::update(
	RE_RenderContext          r,
	const GT_PrimitiveHandle  &primh,
	const GR_UpdateParms      &p)
{
	std::cout << "GR_PrimGsplat::update - primHandle " << primh << std::endl;

	//myPrimitiveHandle = primh.get();

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
		std::cerr << "Color attribute 'Cd' not found!" << std::endl;
		return;
	}
	GA_ROHandleV3 colorHandle(cdAttr);
	if (!colorHandle.isValid()) 
	{
		std::cerr << "Invalid color handle!" << std::endl;
		return;
	}

	const GA_Attribute *alphaAttr = dtl->findPointAttribute("opacity");
	if (!alphaAttr) 
	{
		std::cerr << "Opacity attribute 'opacity' not found!" << std::endl;
		return;
	}
	GA_ROHandleF alphaHandle(alphaAttr);
	if (!alphaHandle.isValid()) 
	{
		std::cerr << "Invalid opacity handle!" << std::endl;
		return;
	}

	const GA_Attribute *scaleAttr = dtl->findPointAttribute("scale");
	if (!scaleAttr) 
	{
		std::cerr << "Scale attribute 'scale' not found!" << std::endl;
		return;
	}
	GA_ROHandleV3 scaleHandle(scaleAttr);
	if (!scaleHandle.isValid()) 
	{
		std::cerr << "Invalid scale handle!" << std::endl;
		return;
	}

	const GA_Attribute *orientAttr = dtl->findPointAttribute("orient");
	if (!orientAttr) 
	{
		std::cerr << "Orientation attribute 'orient' not found!" << std::endl;
		return;
	}
	GA_ROHandleV4 orientHandle(orientAttr);
	if (!orientHandle.isValid()) 
	{
		std::cerr << "Invalid orientation handle!" << std::endl;
		return;
	}

	SHHandles shHandles;
	bool sh_data_found = initAllSHHandles(dtl, shHandles);

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
		[&](const tbb::blocked_range<GA_Size>& r) {
			for (GA_Size i = r.begin(); i != r.end(); ++i) {
				const GA_Offset ptoff = gSplatPrim->getVertexOffset(i);
				const UT_Vector3 pos = dtl->getPos3(ptoff);
				const UT_Vector3 color = colorHandle.get(ptoff);
				const float alpha = alphaHandle.get(ptoff);
				const UT_Vector3 scale = scaleHandle.get(ptoff);
				const UT_Vector4 orient = orientHandle.get(ptoff);

				mySplatPts[i] = UT_Vector3H(pos);
				mySplatColors[i] = UT_Vector3H(color);
				mySplatAlphas[i] = alpha;
				mySplatScales[i] = UT_Vector3H(scale);
				mySplatOrients[i] = UT_Vector4H(orient);

				if (sh_data_found)
				{
					myShxs[i] = UT_Matrix4F(0.0);
					myShys[i] = UT_Matrix4F(0.0);
					myShzs[i] = UT_Matrix4F(0.0);
					for (int j = 0; j < 15; ++j) {
						UT_Vector3 shValue = shHandles.sh[j].get(ptoff);
						int row = int(float(j) / 4);  
						int col = j % 4;
						myShxs[i](row, col) = shValue.x();
						myShys[i](row, col) = shValue.y();
						myShzs[i](row, col) = shValue.z();
					}
				}
			}
		}
	);

	GR_UpdateParms dp(p);

	const int verticesPerQuad_wireframe = 8;
	myWireframeGeo->setNumPoints(myGsplatCount * verticesPerQuad_wireframe);
	RE_VertexArray *posWire = myWireframeGeo->findCachedAttrib(r, posname, RE_GPU_FLOAT16, 3, RE_ARRAY_POINT, true);
	RE_VertexArray *colorWire = myWireframeGeo->findCachedAttrib(r, colorname, RE_GPU_FLOAT16, 3, RE_ARRAY_POINT, true);
    RE_VertexArray *orientWire = myWireframeGeo->findCachedAttrib(r, orientname, RE_GPU_FLOAT16, 4, RE_ARRAY_POINT, true);
	RE_VertexArray *scaleWire = myWireframeGeo->findCachedAttrib(r, scalename, RE_GPU_FLOAT16, 3, RE_ARRAY_POINT, true);

	if(posWire->getCacheVersion() != dp.geo_version)
    {
		UT_Vector3H *pdata = static_cast<UT_Vector3H *>(posWire->map(r));
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

	// GSplatRenderer::getInstance().registerUpdate(generatePrimID(dtl, gSplatPrim), 
	// 									 dp.geo_version, 
	// 									 myGsplatCount, 
	// 									 mySplatPts, 
	// 									 mySplatColors, 
	// 									 mySplatAlphas,
	// 									 mySplatScales,
	// 									 mySplatOrients,
	// 									 myShxs,
	// 									 myShys,
	// 									 myShzs);


	// retrieve primitive index 
	


	myRegistryId = GSplatRenderer::getInstance().registerUpdate(
										 dtl,
										 dp.geo_version, 
										 gSplatPrim->getVertexOffset(0),

										 myGsplatCount, 
										 mySplatPts, 
										 mySplatColors, 
										 mySplatAlphas,
										 mySplatScales,
										 mySplatOrients,
										 myShxs,
										 myShys,
										 myShzs);

	std::cout << "GR_PrimGsplat::update - myRegistryId: " << myRegistryId << std::endl;
}

void
GR_PrimGsplat::render(
	RE_RenderContext	r,
	GR_RenderMode	    render_mode,
	GR_RenderFlags	    flags,
	GR_DrawParms	    dp)
{
	//std::cout << "GR_PrimGsplat::render - primHandle: " << myPrimitiveHandle << std::endl;
	
	if(!myWireframeGeo)
	{
		return;
	}

	GSplatRenderer::getInstance().setRenderingEnabled(render_mode < GR_RENDER_NUM_BEAUTY_MODES); //TODO, pass in r here, as different viewports could have different render modes.

	bool need_wire = (render_mode == GR_RENDER_WIREFRAME ||
		      (flags & GR_RENDER_FLAG_WIRE_OVER));

    if(need_wire)
    {
		RE_Shader* sh = GsplatShaderManager::getInstance().getShader(GsplatShaderManager::GSPLAT_WIRE_SHADER, r);
		r->pushShader(sh);
		myWireframeGeo->draw(r, RE_GEO_WIRE_IDX);

		//TODO: INSTANCED VERSION?
		//myWireframeGeo->drawInstanced(r, RE_GEO_WIRE_IDX, gSplatCount);
		r->popShader();
	}

	GSplatRenderer::getInstance().includeInRenderPass(myRegistryId);

	std::cout << "GR_PrimGsplat::render - myRegistryId: " << myRegistryId << std::endl;
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
