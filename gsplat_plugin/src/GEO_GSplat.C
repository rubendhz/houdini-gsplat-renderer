#include "GEO_GSplat.h"
#include "GR_GSplat.h"

#include <UT/UT_Defines.h>
#include <UT/UT_SparseArray.h>
#include <UT/UT_SysClone.h>
#include <UT/UT_IStream.h>
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_MemoryCounter.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_Vector3.h>
#include <GA/GA_AttributeRefMap.h>
#include <GA/GA_AttributeRefMapDestHandle.h>
#include <GA/GA_Defragment.h>
#include <GA/GA_WorkVertexBuffer.h>
#include <GA/GA_WeightedSum.h>
#include <GA/GA_MergeMap.h>
#include <GA/GA_IntrinsicMacros.h>
#include <GA/GA_ElementWrangler.h>
#include <GA/GA_PrimitiveJSON.h>
#include <GA/GA_RangeMemberQuery.h>
#include <GA/GA_LoadMap.h>
#include <GA/GA_SaveMap.h>
#include <GEO/GEO_ConvertParms.h>
#include <GEO/GEO_Detail.h>
#include <GEO/GEO_ParallelWiringUtil.h>
#include <GEO/GEO_PrimPoly.h>
#include <GEO/GEO_PrimType.h>
#include <DM/DM_RenderTable.h>

#include <iostream>


GA_PrimitiveDefinition *GEO_PrimGsplat::theDefinition = nullptr;


GEO_PrimGsplat::GEO_PrimGsplat(GA_Detail &d, GA_Offset offset)
    : GEO_Primitive(&d, offset)
{
}

GEO_PrimGsplat::~GEO_PrimGsplat()
{
}

void
GEO_PrimGsplat::stashed(bool beingstashed, GA_Offset offset)
{
    GEO_Primitive::stashed(beingstashed, offset);
}

bool
GEO_PrimGsplat::evaluatePointRefMap(GA_Offset result_vtx,
				GA_AttributeRefMap &map,
				fpreal u, fpreal v, unsigned du,
				unsigned dv) const
{
    // This primitive doesn't have a meaningful way to evaluate a point
    // given a u,v coordinate, so return false.
    return false;
}

int
GEO_PrimGsplat::evaluatePointV4(UT_Vector4 &pos,
                float u, float v,
				unsigned du, unsigned dv) const 
{
    // This primitive doesn't have a meaningful way to evaluate a point
    return false;
}

void
GEO_PrimGsplat::reverse()
{
    //This primitive doesn't have a meaningful way to reverse itself,
    //so this implementation is intentionally left empty.
    return;
}

UT_Vector3D
GEO_PrimGsplat::computeNormalD() const
{
    // This primitive doesn't have a meaningful normal derivative to compute,
    // so return a default normal vector in double precision.
    return UT_Vector3D(0, 0, 1);
}

UT_Vector3
GEO_PrimGsplat::computeNormal() const
{
    // This primitive doesn't have a meaningful normal to compute,
    // so return a default normal vector. 
    return UT_Vector3(0, 0, 1);
}

fpreal
GEO_PrimGsplat::calcVolume(const UT_Vector3 &) const
{
   UT_BoundingBox bbox;
   getBBox(&bbox);

   return bbox.volume();
}

fpreal
GEO_PrimGsplat::calcArea() const
{
   UT_BoundingBox bbox;
   getBBox(&bbox);

   return bbox.area();
}

fpreal
GEO_PrimGsplat::calcPerimeter() const
{
    UT_BoundingBox bbox;
    getBBox(&bbox);

    return 4.0 * (bbox.xsize() + bbox.ysize() + bbox.zsize());
}

int
GEO_PrimGsplat::detachPoints(GA_PointGroup &grp)
{
    return 0;
}

GA_Primitive::GA_DereferenceStatus
GEO_PrimGsplat::dereferencePoint(GA_Offset point, bool dry_run)
{
    return GA_DEREFERENCE_OK;
}

GA_Primitive::GA_DereferenceStatus
GEO_PrimGsplat::dereferencePoints(const GA_RangeMemberQuery &point_query, bool dry_run)
{
	return GA_DEREFERENCE_OK;
}


///
/// JSON methods
///

using namespace UT::Literal;

static UT_StringHolder theKWVertex = "vertex"_sh;

class geo_PrimGsplatJSON : public GA_PrimitiveJSON
{
public:
    geo_PrimGsplatJSON()
    {
    }
    ~geo_PrimGsplatJSON() override {}

    enum
    {
	geo_TBJ_VERTEX,
	geo_TBJ_ENTRIES
    };

    const GEO_PrimGsplat	*tet(const GA_Primitive *p) const
			{ return static_cast<const GEO_PrimGsplat *>(p); }
    GEO_PrimGsplat	*tet(GA_Primitive *p) const
			{ return static_cast<GEO_PrimGsplat *>(p); }

    int		         getEntries() const override { return geo_TBJ_ENTRIES; }

    const UT_StringHolder &getKeyword(int i) const override
			{
			    switch (i)
			    {
				case geo_TBJ_VERTEX:	return theKWVertex;
				case geo_TBJ_ENTRIES:	break;
			    }
			    UT_ASSERT(0);
			    return UT_StringHolder::theEmptyString;
			}
    bool saveField(const GA_Primitive *pr, int i,
		   UT_JSONWriter &w, const GA_SaveMap &map) const override
		{
		    switch (i)
		    {
			case geo_TBJ_VERTEX:
			    return tet(pr)->saveVertexArray(w, map);
			case geo_TBJ_ENTRIES:
			    break;
		    }
		    return false;
		}
    bool saveField(const GA_Primitive *pr, int i,
		   UT_JSONValue &v, const GA_SaveMap &map) const override
		{
		    switch (i)
		    {
			case geo_TBJ_VERTEX:
			    return false;
			case geo_TBJ_ENTRIES:
			    break;
		    }
		    UT_ASSERT(0);
		    return false;
		}
    bool loadField(GA_Primitive *pr, int i, UT_JSONParser &p,
		   const GA_LoadMap &map) const override
		{
		    switch (i)
		    {
			case geo_TBJ_VERTEX:
			    return tet(pr)->loadVertexArray(p, map);
			case geo_TBJ_ENTRIES:
			    break;
		    }
		    UT_ASSERT(0);
		    return false;
		}
    bool loadField(GA_Primitive *pr, int i, UT_JSONParser &p,
		   const UT_JSONValue &v, const GA_LoadMap &map) const override
		{
		    switch (i)
		    {
			case geo_TBJ_VERTEX:
			    return false;
			case geo_TBJ_ENTRIES:
			    break;
		    }
		    UT_ASSERT(0);
		    return false;
		}
    bool isEqual(int i, const GA_Primitive *p0,
		 const GA_Primitive *p1) const override
		{
		    switch (i)
		    {
			case geo_TBJ_VERTEX:
			    return false;
			case geo_TBJ_ENTRIES:
			    break;
		    }
		    UT_ASSERT(0);
		    return false;
		}
private:
};


static const GA_PrimitiveJSON *
GsplatJSON()
{
    static GA_PrimitiveJSON *theJSON = NULL;

    if (!theJSON)
	theJSON = new geo_PrimGsplatJSON();
    return theJSON;
}

const GA_PrimitiveJSON *
GEO_PrimGsplat::getJSON() const
{
    return GsplatJSON();
}


bool
GEO_PrimGsplat::saveVertexArray(UT_JSONWriter &w,
		const GA_SaveMap &map) const
{
    return myVertexList.jsonVertexArray(w, map);
}

bool GEO_PrimGsplat::loadVertexArray(UT_JSONParser &p, const GA_LoadMap &map)
{
    GA_Offset startvtxoff = map.getVertexOffset();

    // Signal the start of array parsing.
    bool error = false;
    if (!p.parseBeginArray(error) || error) {
        return false; // Early exit if we're not starting with an array.
    }

    std::vector<GA_Offset> vtxOffsets;
    int64 vtxOff;

    // Since parseInt64 isn't available, we use parseInteger (or parseNumber if dealing with specific types).
    // Loop to parse each integer in the array, breaking when a parse fails, which we take as reaching the end of the array.
    while (!error) {
        if (!p.parseInteger(vtxOff)) {
            // If parsing fails, we assume it's because we've reached the end of the array.
            break;
        }

        if (startvtxoff != GA_Offset(0)) {
            // Adjust vertex offset by starting offset if necessary.
            vtxOff += startvtxoff;
        }

        // Add adjusted vertex offset to our vector.
        vtxOffsets.push_back(GA_Offset(vtxOff));
    }

    // Conclude array parsing. The error flag isn't strictly needed here, but included for consistency.
    p.parseEndArray(error);

    // At this point, vtxOffsets contains all vertex offsets.
    // Proceed to integrate these offsets into your primitive as needed.
    // This step is specific to your implementation and needs.
    
    // Check if any vertex offsets were successfully parsed.
    return !vtxOffsets.empty();
}

bool
GEO_PrimGsplat::getBBox(UT_BoundingBox *bbox) const
{
    GA_Size vtx_count = getVertexCount();
    if (!vtx_count)
        return false;
    bbox->initBounds(getPos3(0));
    for (int i = 0; i < vtx_count; ++i)
        bbox->enlargeBounds(getPos3(i));
    return true;
}

UT_Vector3
GEO_PrimGsplat::baryCenter() const
{
    UT_Vector3 sum(0,0,0);

    const GA_Size npts = getVertexCount();

    for (int i = 0; i < npts; ++i)
        sum += getPos3(i);

    sum /= npts;

    return sum;
}

bool
GEO_PrimGsplat::isDegenerate() const
{
    // No points means degenerate.
    return getDetail().getNumPoints() == 0;
}

void
GEO_PrimGsplat::copyPrimitive(const GEO_Primitive *psrc)
{
    if (psrc == this)
        return;

    // This sets the number of vertices to be the same as psrc, and wires
    // the corresponding vertices to the corresponding points.
    // This class doesn't have any more data, so we didn't need to
    // override copyPrimitive, but if you add any more data, copy it
    // below.
    GEO_Primitive::copyPrimitive(psrc);

    // Uncomment this to access other data
    //const GEO_PrimGsplat *src = (const GEO_PrimGsplat *)psrc;
}

GEO_Primitive *
GEO_PrimGsplat::copy(int preserve_shared_pts) const
{
    GEO_Primitive *clone = GEO_Primitive::copy(preserve_shared_pts);

    if (!clone)
        return nullptr;

    // This class doesn't have any more data to copy, so we didn't need
    // to override this function, but if you add any, copy them here.

    // Uncomment this to access other data
    //GEO_PrimGsplat *tet = (GEO_PrimGsplat*)clone;

    return clone;
}

void
GEO_PrimGsplat::copySubclassData(const GA_Primitive *source)
{
    UT_ASSERT( source != this );

    // The superclass' implementation in this case doesn't do anything,
    // but if you have a superclass with member data, it's important
    // to call this here.
    GEO_Primitive::copySubclassData(source);

    // If you add any data that must be copyied from src in order for this
    // to be a separate but identical copy of src, copy it here,
    // but make sure it's safe to call copySubclassData on multiple
    // primitives at the same time.

    // Uncomment this to access other data
    //const GEO_PrimGsplat *src = static_cast<const GEO_PrimGsplat *>(prim_src);
}

GEO_PrimGsplat *GEO_PrimGsplat::build(GA_Detail *gdp)
{   
    GEO_PrimGsplat *gsplat_prim = static_cast<GEO_PrimGsplat *>(gdp->appendPrimitive(GEO_PrimGsplat::theTypeId()));
    
    // TODO: Assume all points are good. Probably we want to filter bad points from the outside 
    // (for instacen, missing splat attributes)
    GA_Size point_count = gdp->getNumPoints();

    GA_Offset vtxoff = gdp->appendVertexBlock(point_count);
    gsplat_prim->myVertexList.setTrivial(vtxoff, point_count);

    for (GA_Size i = 0; i < point_count; ++i) {
        GA_Offset ptOffset = gdp->pointOffset(i); // This assumes points are sequentially numbered which might not always hold true
        // Associate this point offset with the corresponding vertex of the primitive
        gdp->getTopology().wireVertexPoint(vtxoff + i, ptOffset);
    }

    return gsplat_prim;
}

// Static callback for our factory.
static void
geoNewPrimGsplatBlock(
    GA_Primitive **new_prims,
    GA_Size nprimitives,
    GA_Detail &gdp,
    GA_Offset start_offset,
    const GA_PrimitiveDefinition &def,
    bool allowed_to_parallelize)
{
    if (allowed_to_parallelize && nprimitives >= 4*GA_PAGE_SIZE)
    {
        // Allocate them in parallel if we're allocating many.
        // This is using the C++11 lambda syntax to make a functor.
        UTparallelForLightItems(UT_BlockedRange<GA_Offset>(start_offset, start_offset+nprimitives),
            [new_prims,&gdp,start_offset](const UT_BlockedRange<GA_Offset> &r){
                GA_Offset primoff(r.begin());
                GA_Primitive **pprims = new_prims+(primoff-start_offset);
                GA_Offset endprimoff(r.end());
                for ( ; primoff != endprimoff; ++primoff, ++pprims)
                    *pprims = new GEO_PrimGsplat(gdp, primoff);
            });
    }
    else
    {
        // Allocate them serially if we're only allocating a few.
        GA_Offset endprimoff(start_offset + nprimitives);
        for (GA_Offset primoff(start_offset); primoff != endprimoff; ++primoff, ++new_prims)
            *new_prims = new GEO_PrimGsplat(gdp, primoff);
    }
}

void
GEO_PrimGsplat::registerMyself(GA_PrimitiveFactory *factory)
{
    // Ignore double registration
    if (theDefinition)
	return;

    theDefinition = factory->registerDefinition(
        "GSplat",
        geoNewPrimGsplatBlock,
        GA_FAMILY_NONE,
        "GSplat");

    // NOTE: Calling setHasLocalTransform(false) is redundant,
    //       but if your custom primitive has a local transform,
    //       it must call setHasLocalTransform(true).
    theDefinition->setHasLocalTransform(false);
    registerIntrinsics(*theDefinition);
// #ifndef Gsplat_GR_PRIMITIVE
    
//     // Register the GT tesselation too (now we know what type id we have)
//     GT_PrimGsplatCollect::registerPrimitive(theDefinition->getId());
    
// #else

    // Since we're only registering one hook, the priority does not matter.

    const int hook_priority = 0;
    
    DM_RenderTable::getTable()->registerGEOHook(
        new GR_PrimGsplatHook,
        theDefinition->getId(),
        hook_priority,
        GUI_HOOK_FLAG_NONE);

//#endi
}

int64
GEO_PrimGsplat::getMemoryUsage() const
{
    // NOTE: The only memory owned by this primitive is itself
    //       and its base class.
    int64 mem = sizeof(*this) + getBaseMemoryUsage();
    return mem;
}

void
GEO_PrimGsplat::countMemory(UT_MemoryCounter &counter) const
{
    // NOTE: There's no shared memory in this primitive,
    //       apart from possibly in the case class.
    counter.countUnshared(sizeof(*this));
    countBaseMemory(counter);
}

GEO_Primitive *
GEO_PrimGsplat::convertNew(GEO_ConvertParms &parms)
{
    return nullptr;
}

GEO_Primitive *
GEO_PrimGsplat::convert(GEO_ConvertParms &parms, GA_PointGroup *usedpts)
{
    return nullptr;
}

void
GEO_PrimGsplat::normal(NormalComp &output) const
{
    // No need here.
}

void
GEO_PrimGsplat::normal(NormalCompD &output) const
{
    // No need here.
}

int
GEO_PrimGsplat::intersectRay(const UT_Vector3 &org, const UT_Vector3 &dir,
		float tmax, float , float *distance,
		UT_Vector3 *pos, UT_Vector3 *nml,
		int, float *, float *, int) const
{
    UT_BoundingBox bbox;
    getBBox(&bbox);

    float dist;
    int result =  bbox.intersectRay(org, dir, tmax, &dist, nml);
    if (result)
    {
	if (distance) *distance = dist;
	if (pos) *pos = org + dist * dir;
    }
    return result;
}

void 
GEO_PrimGsplat::addPointOffset(GA_Offset offset) {
    // Example: storing the offset in a vector (or another suitable data structure)
    // Ensure you have a data structure like std::vector<GA_Offset> pointOffsets; in your class definition
    //pointOffsets.push_back(offset);
    return;
}


// This is the usual DSO hook.
extern "C" {
void
newGeometryPrim(GA_PrimitiveFactory *factory)
{
    GEO_PrimGsplat::registerMyself(factory);
}
}

// Implement intrinsic attributes
enum
{
    geo_INTRINSIC_ADDRESS,	// Return the address of the primitive
    geo_INTRINSIC_AUTHOR,	// Developer's name
    geo_NUM_INTRINSICS		// Number of intrinsics
};

namespace
{
    static int64
    intrinsicAddress(const GEO_PrimGsplat *prim)
    {
	// An intrinsic attribute which returns the address of the primitive
	return (int64)prim;
    }
    static const char *
    intrinsicAuthor(const GEO_PrimGsplat *)
    {
	// An intrinsic attribute which returns the HDK author's name
	return "ruben";
    }
};

// Start defining intrinsic attributes, we pass our class name and the number
// of intrinsic attributes.
GA_START_INTRINSIC_DEF(GEO_PrimGsplat, geo_NUM_INTRINSICS)

    // See GA_IntrinsicMacros.h for further information on how to define
    // intrinsic attribute evaluators.
    GA_INTRINSIC_I(GEO_PrimGsplat, geo_INTRINSIC_ADDRESS, "address",
		intrinsicAddress)
    GA_INTRINSIC_S(GEO_PrimGsplat, geo_INTRINSIC_AUTHOR, "author",
		intrinsicAuthor)

// End intrinsic definitions (our class and our base class)
GA_END_INTRINSIC_DEF(GEO_PrimGsplat, GEO_Primitive)
